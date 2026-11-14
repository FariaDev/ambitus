/*
 * Ambitus
 *
 * Overlay of hold-to-straighten (StrokeHandler): the violet ring at the pen tip while the pen is held, then the
 * cross-fade from the hand-drawn stroke to the clean shape. It is drawn over the page and is never part of the
 * document: it takes no place in the undo, is not saved, and goes away by itself when the handler says the shape
 * is in the page.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>

#include <cairo.h>

#include "model/Stroke.h"
#include "util/DispatchPool.h"
#include "util/Range.h"

#include "OverlayView.h"

class StrokeHandler;

namespace xoj::view {
class Repaintable;

class HoldSnapView: public ToolView, public xoj::util::Listener<HoldSnapView> {
public:
    HoldSnapView(const StrokeHandler* handler, Repaintable* parent);
    ~HoldSnapView() noexcept override;

    bool isViewOf(const OverlayBase* overlay) const override;
    void draw(cairo_t* cr) const override;

    /// Where the ring is drawn, and how it looks (screen pixels)
    static constexpr double RING_RADIUS = 10;
    static constexpr double RING_WIDTH = 2;
    static constexpr double RING_ALPHA = .6;

    /// The ring at the pen tip (page coordinates), until it is hidden or the fade starts
    static constexpr struct ShowRingRequest {
    } SHOW_RING_REQUEST = {};
    void on(ShowRingRequest, double x, double y);

    static constexpr struct HideRingRequest {
    } HIDE_RING_REQUEST = {};
    void on(HideRingRequest);

    /// The hand-drawn stroke goes out in 120 ms and the shape comes in in 180 ms, both at once
    static constexpr struct StartFadeRequest {
    } START_FADE_REQUEST = {};
    void on(StartFadeRequest, const Stroke& from, const Stroke& to);

    /// The frame clock of the handler: repaints what moves
    static constexpr struct TickRequest {
    } TICK_REQUEST = {};
    void on(TickRequest);

    /// The shape is now in the page: draws it whole into the page buffer, so that nothing blinks, and goes away
    static constexpr struct FinishRequest {
    } FINISH_REQUEST = {};
    void deleteOn(FinishRequest);

    /// Goes away without leaving anything on the page
    static constexpr struct DismissRequest {
    } DISMISS_REQUEST = {};
    void deleteOn(DismissRequest);

    /// How long the fade has run, in microseconds (0 before it starts)
    gint64 fadeElapsedUs() const;

    /// The part of the page that the ring or the fade can touch
    Range dirtyRange() const;

private:
    const StrokeHandler* handler;

    bool ringOn = false;
    double ringX = 0;
    double ringY = 0;

    bool fading = false;
    bool finished = false;  ///< the shape is in the page: draw it whole, nothing else
    gint64 fadeStart = 0;   ///< monotonic time, µs
    std::unique_ptr<Stroke> from;
    std::unique_ptr<Stroke> to;
};
};  // namespace xoj::view
