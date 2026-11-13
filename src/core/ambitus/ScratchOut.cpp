#include "ScratchOut.h"

#include <algorithm>
#include <cmath>

namespace ambitus::scratch {

namespace {
constexpr int MIN_REVERSALS = 4;
constexpr double REVERSAL_SHARE = 1.0 / 3.0;  ///< a turn counts after moving back this share of the length
constexpr double COVERED_SHARE = 0.6;         ///< share of the target that must be inside the scribble box
constexpr double BOX_MARGIN = 0.15;           ///< the box grows by this share of its size on each side

double dot(Pt a, Pt b) { return a.x * b.x + a.y * b.y; }
Pt sub(Pt a, Pt b) { return {a.x - b.x, a.y - b.y}; }
}  // namespace

bool detectScribble(const std::vector<Pt>& points, Scribble& out, double minSize) {
    if (points.size() < 8) {
        return false;
    }
    // main direction: principal axis of the points
    Pt c{0, 0};
    for (auto const& p: points) {
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
    for (auto const& p: points) {
        Pt d = sub(p, c);
        double a = dot(d, dir);
        double t = dot(d, normal);
        along.push_back(a);
        s.minS = std::min(s.minS, a);
        s.maxS = std::max(s.maxS, a);
        s.minT = std::min(s.minT, t);
        s.maxT = std::max(s.maxT, t);
    }
    double length = s.maxS - s.minS;
    if (length < minSize) {
        return false;
    }

    // count the turns along dir, with hysteresis so that jitter does not count
    double threshold = REVERSAL_SHARE * length;
    int direction = 0;  // +1 forward, -1 back, 0 not moved enough yet
    double lo = along.front(), hi = along.front(), extreme = along.front();
    for (double a: along) {
        if (direction == 0) {
            lo = std::min(lo, a);
            hi = std::max(hi, a);
            if (hi - lo > threshold) {
                direction = a >= hi ? 1 : -1;
                extreme = a;
            }
        } else if (direction * (a - extreme) > 0) {
            extreme = a;  // still moving the same way
        } else if (std::abs(a - extreme) > threshold) {
            s.reversals++;
            direction = -direction;
            extreme = a;
        }
    }
    if (s.reversals < MIN_REVERSALS) {
        return false;
    }
    out = s;
    return true;
}

bool crossesOut(const Scribble& s, const std::vector<Pt>& scribble, const std::vector<Pt>& target, double tolerance) {
    if (target.empty() || scribble.empty()) {
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
    // the scribble must actually pass over the target, not just around it
    double tol2 = tolerance * tolerance;
    for (auto const& p: target) {
        for (auto const& q: scribble) {
            Pt d = sub(p, q);
            if (dot(d, d) <= tol2) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace ambitus::scratch
