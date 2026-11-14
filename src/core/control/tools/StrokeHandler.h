/*
 * Xournal++
 *
 * Handles input of strokes
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr

#include <gdk/gdk.h>  // for GdkEventKey

#include "model/PageRef.h"  // for PageRef
#include "model/Point.h"    // for Point

#include "InputHandler.h"            // for InputHandler
#include "SnapToGridInputHandler.h"  // for SnapToGridInputHandler

class Control;
class Layer;
class PositionInputData;
class Stroke;

namespace xoj::util {
template <class T>
class DispatchPool;
};

namespace xoj::view {
class HoldSnapView;
class OverlayView;
class Repaintable;
class StrokeToolView;
};  // namespace xoj::view

namespace StrokeStabilizer {
class Base;
class Active;
}  // namespace StrokeStabilizer

/**
 * @brief The stroke handler draws a stroke on a XojPageView
 *
 * The stroke is drawn using a cairo_surface_t* as a mask:
 * As the pointer moves on the canvas single segments are
 * drawn opaquely on the initially transparent masking
 * surface. The surface is used to mask the stroke
 * when drawing it to the XojPageView
 */
class StrokeHandler: public InputHandler {
public:
    StrokeHandler(Control* control, const PageRef& page);
    ~StrokeHandler() override;

    void onSequenceCancelEvent() override;
    bool onMotionNotifyEvent(const PositionInputData& pos, double zoom) override;
    void onButtonReleaseEvent(const PositionInputData& pos, double zoom) override;
    void onButtonPressEvent(const PositionInputData& pos, double zoom) override;
    void onButtonDoublePressEvent(const PositionInputData& pos, double zoom) override;
    bool onKeyPressEvent(const KeyEvent& event) override;
    bool onKeyReleaseEvent(const KeyEvent& event) override;

    /**
     * @brief Add a straight line to the stroke (if the movement is valid).
     * The line may be subdivided into smaller segments if the pressure variation is too big.
     * @param point The endpoint of the added line
     */
    void paintTo(Point point);

    auto createView(xoj::view::Repaintable* parent) const -> std::unique_ptr<xoj::view::OverlayView> override;

    const std::shared_ptr<xoj::util::DispatchPool<xoj::view::StrokeToolView>>& getViewPool() const;

    /// Ambitus: the pool of the overlay of hold-to-straighten (the ring, then the fade)
    const std::shared_ptr<xoj::util::DispatchPool<xoj::view::HoldSnapView>>& getSnapPool() const;

protected:
    /**
     * @brief Unconditionally add a segment to the stroke.
     * Warning: it does not set the width properly nor test if the motion is valid. Use paintTo instead.
     * @param point The endpoint of the added segment
     */
    void drawSegmentTo(const Point& point);

    void strokeRecognizerDetected(std::unique_ptr<Stroke> recognized, Layer* layer);
    /// Sets the width of a recognized shape and snaps it to the grid, if that is on
    void prepareRecognized(Stroke* recognized);
    /// Puts a prepared shape in the page in place of the hand-drawn stroke, with the undo action
    void finishRecognized(std::unique_ptr<Stroke> recognized, Layer* layer);

    /// Ambitus: if the stroke is a scribble over other strokes, erases them and drops the scribble
    bool scratchOut(Layer* layer);

    /// Finalizes the stroke using the provided pressure as last point
    void finalizeStroke(double pressure);

    /// Ambitus: (re)starts the wait of hold-to-shape from this point of the page
    void restartHold(double x, double y, double zoom);
    void stopHold();
    /// Ambitus: the pen stayed still on the page: if the stroke is a shape, it becomes the shape (with a cross-fade)
    void holdToShape();
    /// Ambitus: half way through the hold, if the stroke is a shape, a ring shows at the pen tip
    void showHoldRing();
    /// Ambitus: creates the overlay of hold-to-straighten on the view of the page, if it is not there yet
    bool ensureSnapView();
    /// Ambitus: the fade is over (or cut short): the shape goes into the page, and the overlay goes away
    void commitSnap();
    void snapTick();

protected:
    Point buttonDownPoint;  // used for tapSelect and filtering - never snapped to grid.
    SnapToGridInputHandler snappingHandler;

    bool hasPressure;

    gint64 startedAt = 0;  ///< Ambitus: when the stroke began (monotonic, µs), for scratch out

    guint holdSource = 0;  ///< Ambitus: the timeout of hold-to-shape
    guint ringSource = 0;  ///< Ambitus: the timeout of the ring, half way through the hold
    double holdX = 0;      ///< Ambitus: where the pen is held (page coordinates)
    double holdY = 0;

    // Ambitus: while the shape fades in, it is not in the page yet: the undo, the layer and the repaint wait for the
    // end of the fade (180 ms, or the release of the pen if it comes first). The gesture goes on without a stroke.
    bool snapPending = false;
    guint snapSource = 0;  ///< the frame timer of the fade
    gint64 snapStart = 0;  ///< monotonic time, µs
    std::unique_ptr<Stroke> snapShape;

private:
    /**
     * @brief Pointer to the Stabilizer instance
     */
    std::unique_ptr<StrokeStabilizer::Base> stabilizer;

    std::shared_ptr<xoj::util::DispatchPool<xoj::view::StrokeToolView>> viewPool;
    std::shared_ptr<xoj::util::DispatchPool<xoj::view::HoldSnapView>> snapPool;

    friend class StrokeStabilizer::Active;

    static constexpr double MAX_WIDTH_VARIATION = 0.3;
};
