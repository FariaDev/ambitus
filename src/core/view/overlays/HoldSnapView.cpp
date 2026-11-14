#include "HoldSnapView.h"

#include <algorithm>

#include <glib.h>

#include "control/tools/StrokeHandler.h"
#include "gui/AmbitusMotion.h"
#include "view/Repaintable.h"
#include "view/StrokeView.h"
#include "view/View.h"

using namespace xoj::view;

namespace {
/// Paints the stroke as one piece with the given opacity: overlaps of its own segments do not show through
void paintWithAlpha(cairo_t* cr, const Stroke& s, double alpha) {
    if (alpha <= 0) {
        return;
    }
    if (alpha >= 1) {
        StrokeView(&s).draw(Context::createDefault(cr));
        return;
    }
    cairo_push_group(cr);
    StrokeView(&s).draw(Context::createDefault(cr));
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
}
}  // namespace

HoldSnapView::HoldSnapView(const StrokeHandler* handler, Repaintable* parent): ToolView(parent), handler(handler) {
    this->registerToPool(handler->getSnapPool());
}

HoldSnapView::~HoldSnapView() noexcept { this->unregisterFromPool(); }

bool HoldSnapView::isViewOf(const OverlayBase* overlay) const { return overlay == this->handler; }

gint64 HoldSnapView::fadeElapsedUs() const { return fading ? g_get_monotonic_time() - fadeStart : 0; }

Range HoldSnapView::dirtyRange() const {
    Range rg;
    if (ringOn) {
        Range r(ringX, ringY);
        r.addPadding((RING_RADIUS + RING_WIDTH) / parent->getZoom());
        rg = r;
    }
    if (from && to) {
        rg = rg.unite(Range(from->getBoundingBox())).unite(Range(to->getBoundingBox()));
    }
    return rg;
}

void HoldSnapView::draw(cairo_t* cr) const {
    if (finished) {
        if (to) {
            paintWithAlpha(cr, *to, 1);
        }
        return;
    }
    if (fading && from && to) {
        double us = static_cast<double>(fadeElapsedUs());
        // the stroke goes out in 120 ms and the shape comes in in 180 ms, side by side, each with its own curve
        double out = ambitus::motion::easeOut(std::clamp(us / static_cast<double>(ambitus::motion::FAST_US), 0., 1.));
        double in = ambitus::motion::easeOut(std::clamp(us / static_cast<double>(ambitus::motion::BASE_US), 0., 1.));
        paintWithAlpha(cr, *from, 1 - out);
        paintWithAlpha(cr, *to, in);
    }
    if (ringOn) {
        double zoom = parent->getZoom();
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0x80 / 255., 0x44 / 255., 0xca / 255., RING_ALPHA);
        cairo_set_line_width(cr, RING_WIDTH / zoom);
        cairo_new_sub_path(cr);
        cairo_arc(cr, ringX, ringY, RING_RADIUS / zoom, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
}

void HoldSnapView::on(ShowRingRequest, double x, double y) {
    ringOn = true;
    ringX = x;
    ringY = y;
    parent->flagDirtyRegion(dirtyRange());
}

void HoldSnapView::on(HideRingRequest) {
    Range rg = dirtyRange();
    ringOn = false;
    parent->flagDirtyRegion(rg);
}

void HoldSnapView::on(StartFadeRequest, const Stroke& fromStroke, const Stroke& toStroke) {
    Range rg = dirtyRange();
    ringOn = false;
    fading = true;
    fadeStart = g_get_monotonic_time();
    from = fromStroke.cloneStroke();
    to = toStroke.cloneStroke();
    parent->flagDirtyRegion(rg.unite(dirtyRange()));
}

void HoldSnapView::on(TickRequest) { parent->flagDirtyRegion(dirtyRange()); }

void HoldSnapView::deleteOn(FinishRequest) {
    Range rg = dirtyRange();
    fading = false;
    ringOn = false;
    finished = true;
    this->parent->drawAndDeleteToolView(this, rg);
}

void HoldSnapView::deleteOn(DismissRequest) {
    Range rg = dirtyRange();
    this->parent->deleteOverlayView(this, rg);
}
