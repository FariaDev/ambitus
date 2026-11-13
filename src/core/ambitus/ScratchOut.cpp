#include "ScratchOut.h"

#include <algorithm>
#include <cmath>

namespace ambitus::scratch {

namespace {
constexpr size_t MIN_PASSES = 5;
constexpr double PASS_SHARE = 0.4;     ///< a pass must cover this share of the length
constexpr double MAX_FLATNESS = 0.65;  ///< extent across / extent along
constexpr double MIN_PATH = 4.0;       ///< the path is at least this many times the length
constexpr double COVERED_SHARE = 0.6;  ///< share of the target that must be inside the scribble box
constexpr double BOX_MARGIN = 0.15;    ///< the box grows by this share of its size on each side

double dot(Pt a, Pt b) { return a.x * b.x + a.y * b.y; }
Pt sub(Pt a, Pt b) { return {a.x - b.x, a.y - b.y}; }

double distanceToSegment2(Pt p, Pt a, Pt b) {
    Pt ab = sub(b, a);
    Pt ap = sub(p, a);
    double len2 = dot(ab, ab);
    double t = len2 > 0 ? std::clamp(dot(ap, ab) / len2, 0.0, 1.0) : 0.0;
    Pt d{ap.x - t * ab.x, ap.y - t * ab.y};
    return dot(d, d);
}
}  // namespace

bool detectScribble(const std::vector<Pt>& points, Scribble& out, double minSize) {
    if (points.size() < 8) {
        return false;
    }
    // main direction: principal axis of the points
    Pt c{0, 0};
    for (auto const& p: points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return false;
        }
        c.x += p.x;
        c.y += p.y;
    }
    c.x /= static_cast<double>(points.size());
    c.y /= static_cast<double>(points.size());
    double sxx = 0, syy = 0, sxy = 0;
    for (auto const& p: points) {
        Pt d = sub(p, c);
        sxx += d.x * d.x;
        syy += d.y * d.y;
        sxy += d.x * d.y;
    }
    double angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
    Pt dir{std::cos(angle), std::sin(angle)};
    Pt normal{-dir.y, dir.x};

    Scribble s;
    s.origin = c;
    s.dir = dir;
    s.minS = s.minT = 1e300;
    s.maxS = s.maxT = -1e300;
    std::vector<double> along;
    along.reserve(points.size());
    double path = 0;
    for (size_t i = 0; i < points.size(); i++) {
        Pt d = sub(points[i], c);
        double a = dot(d, dir);
        double t = dot(d, normal);
        along.push_back(a);
        s.minS = std::min(s.minS, a);
        s.maxS = std::max(s.maxS, a);
        s.minT = std::min(s.minT, t);
        s.maxT = std::max(s.maxT, t);
        if (i > 0) {
            path += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
        }
    }
    double length = s.maxS - s.minS;
    if (length < minSize || s.maxT - s.minT > MAX_FLATNESS * length || path < MIN_PATH * length) {
        return false;
    }

    // split into passes at the turns, with hysteresis so that jitter does not count
    double threshold = PASS_SHARE * length;
    int direction = 0;  // +1 forward, -1 back, 0 not moved enough yet
    size_t start = 0, extremeAt = 0;
    double extreme = along.front();
    s.turns.push_back(0);
    for (size_t i = 1; i < along.size(); i++) {
        double a = along[i];
        if (direction == 0) {
            if (std::abs(a - along[start]) >= threshold) {
                direction = a > along[start] ? 1 : -1;
                extreme = a;
                extremeAt = i;
            }
        } else if (direction * (a - extreme) > 0) {
            extreme = a;  // still moving the same way
            extremeAt = i;
        } else if (direction * (extreme - a) >= threshold) {
            s.turns.push_back(extremeAt);
            start = extremeAt;
            direction = -direction;
            extreme = a;
            extremeAt = i;
        }
    }
    if (direction != 0 && std::abs(extreme - along[start]) >= threshold) {
        s.turns.push_back(extremeAt);  // the last pass
    }
    if (s.passes() < MIN_PASSES) {
        return false;
    }
    out = std::move(s);
    return true;
}

bool crossesOut(const Scribble& s, const std::vector<Pt>& scribble, const std::vector<Pt>& target, double tolerance) {
    if (target.empty() || scribble.empty() || s.passes() == 0) {
        return false;
    }
    Pt normal{-s.dir.y, s.dir.x};
    double ms = BOX_MARGIN * (s.maxS - s.minS) + tolerance;
    double mt = BOX_MARGIN * (s.maxT - s.minT) + tolerance;
    size_t inside = 0;
    for (auto const& p: target) {
        Pt d = sub(p, s.origin);
        double a = dot(d, s.dir);
        double t = dot(d, normal);
        if (a >= s.minS - ms && a <= s.maxS + ms && t >= s.minT - mt && t <= s.maxT + mt) {
            inside++;
        }
    }
    if (static_cast<double>(inside) < COVERED_SHARE * static_cast<double>(target.size())) {
        return false;
    }

    // two passes that are not neighbours must go over it: neighbours share a turning point
    double tol2 = tolerance * tolerance;
    auto passHits = [&](size_t k) {
        size_t from = s.turns[k], to = std::min(s.turns[k + 1], scribble.size() - 1);
        for (size_t i = from; i < to; i++) {
            for (auto const& p: target) {
                if (distanceToSegment2(p, scribble[i], scribble[i + 1]) <= tol2) {
                    return true;
                }
            }
        }
        return false;
    };
    long firstHit = -1;
    for (size_t k = 0; k < s.passes(); k++) {
        if (!passHits(k)) {
            continue;
        }
        if (firstHit < 0) {
            firstHit = static_cast<long>(k);
        } else if (static_cast<long>(k) >= firstHit + 2) {
            return true;
        }
    }
    return false;
}

}  // namespace ambitus::scratch
