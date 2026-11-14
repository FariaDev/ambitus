#include "StrokeHandler.h"

#include <algorithm>  // for max, min
#include <cmath>      // for ceil, pow, abs
#include <limits>     // for numeric_limits
#include <memory>     // for unique_ptr, mak...
#include <utility>    // for move
#include <vector>     // for vector

#include <gdk/gdk.h>  // for GdkEventKey

#include "ambitus/ScratchOut.h"                       // for detectScribble, crossesOut
#include "control/Control.h"                          // for Control
#include "control/ToolEnums.h"                        // for DRAWING_TYPE_ST...
#include "control/ToolHandler.h"                      // for ToolHandler
#include "control/layer/LayerController.h"            // for LayerController
#include "control/settings/Settings.h"                // for Settings
#include "control/settings/SettingsEnums.h"           // for EmptyLastPageAppendType
#include "control/shaperecognizer/ShapeRecognizer.h"  // for ShapeRecognizer
#include "control/tools/InputHandler.h"               // for InputHandler::P...
#include "control/tools/SnapToGridInputHandler.h"     // for SnapToGridInput...
#include "gui/AmbitusMotion.h"                        // for reducedMotion, BASE_US
#include "gui/MainWindow.h"                           // for MainWindow
#include "gui/PageView.h"                             // for XojPageView
#include "gui/XournalView.h"                          // for XournalView
#include "gui/inputdevices/PositionInputData.h"       // for PositionInputData
#include "model/Document.h"                           // for Document
#include "model/Element.h"
#include "model/Layer.h"                                    // for Layer
#include "model/LineStyle.h"                                // for LineStyle
#include "model/Stroke.h"                                   // for Stroke, STROKE_...
#include "model/XojPage.h"                                  // for XojPage
#include "undo/DeleteUndoAction.h"                          // for DeleteUndoAction
#include "undo/InsertUndoAction.h"                          // for InsertUndoAction
#include "undo/RecognizerUndoAction.h"                      // for RecognizerUndoA...
#include "undo/UndoRedoHandler.h"                           // for UndoRedoHandler
#include "util/Assert.h"                                    // for xoj_assert
#include "util/DispatchPool.h"                              // for DispatchPool
#include "util/Range.h"                                     // for Range
#include "util/Rectangle.h"                                 // for Rectangle, util
#include "view/overlays/HoldSnapView.h"                     // for HoldSnapView
#include "view/overlays/StrokeToolFilledHighlighterView.h"  // for StrokeToolFilledHighlighterView
#include "view/overlays/StrokeToolFilledView.h"             // for StrokeToolFilledView
#include "view/overlays/StrokeToolView.h"                   // for StrokeToolView

#include "StrokeStabilizer.h"  // for Base, get

using xoj::util::Rectangle;

StrokeHandler::StrokeHandler(Control* control, const PageRef& page):
        InputHandler(control, page),
        snappingHandler(control->getSettings()),
        stabilizer(StrokeStabilizer::get(control->getSettings())),
        viewPool(std::make_shared<xoj::util::DispatchPool<xoj::view::StrokeToolView>>()),
        snapPool(std::make_shared<xoj::util::DispatchPool<xoj::view::HoldSnapView>>()) {
    snappingHandler.setPageRef(page);
}

StrokeHandler::~StrokeHandler() {
    stopHold();
    if (snapSource) {
        g_source_remove(snapSource);  // a shape that was still fading in is lost with the stroke, as any unfinished one
        snapSource = 0;
    }
    snapPool->dispatchAndClear(xoj::view::HoldSnapView::DISMISS_REQUEST);
}

auto StrokeHandler::onKeyPressEvent(const KeyEvent&) -> bool { return false; }
auto StrokeHandler::onKeyReleaseEvent(const KeyEvent&) -> bool { return false; }

auto StrokeHandler::onMotionNotifyEvent(const PositionInputData& pos, double zoom) -> bool {
    if (!stroke || snapPending) {
        return false;  // the shape is fading in: the rest of the gesture finds no stroke
    }

    if (pos.pressure == 0) {
        /**
         * Some devices emit a move event with pressure 0 when lifting the stylus tip
         * Ignore those events
         */
        return true;
    }

    restartHold(pos.x, pos.y, zoom);
    stabilizer->processEvent(pos);
    return true;
}

namespace {
/// Ambitus: how long the pen stays still to turn the stroke into a shape, and how still (screen pixels)
constexpr guint HOLD_MS = 550;
constexpr double HOLD_TOLERANCE = 4.0;
}  // namespace

void StrokeHandler::restartHold(double x, double y, double zoom) {
    x /= zoom;
    y /= zoom;
    if (holdSource && std::hypot(x - holdX, y - holdY) * zoom < HOLD_TOLERANCE) {
        return;  // a tremor of the hand: still held
    }
    stopHold();
    // only the pen itself: the laser pointer draws with a StrokeHandler too, and its strokes are not ink
    if (!stroke || stroke->getToolType() != StrokeTool::PEN || control->getToolHandler()->getToolType() != TOOL_PEN ||
        control->getToolHandler()->getDrawingType() != DRAWING_TYPE_DEFAULT ||
        !control->getSettings()->getAmbitusHoldToSnap()) {
        return;
    }
    holdX = x;
    holdY = y;
    ringSource = g_timeout_add(
            HOLD_MS / 2,
            +[](gpointer self) -> gboolean {
                auto* h = static_cast<StrokeHandler*>(self);
                h->ringSource = 0;
                h->showHoldRing();
                return G_SOURCE_REMOVE;
            },
            this);
    holdSource = g_timeout_add(
            HOLD_MS,
            +[](gpointer self) -> gboolean {
                auto* h = static_cast<StrokeHandler*>(self);
                h->holdSource = 0;
                h->holdToShape();
                return G_SOURCE_REMOVE;
            },
            this);
}

void StrokeHandler::stopHold() {
    if (holdSource) {
        g_source_remove(holdSource);
        holdSource = 0;
    }
    if (ringSource) {
        g_source_remove(ringSource);
        ringSource = 0;
    }
    if (!snapPending) {
        snapPool->dispatchAndClear(xoj::view::HoldSnapView::DISMISS_REQUEST);  // the ring, if it was there
    }
}

bool StrokeHandler::ensureSnapView() {
    if (!snapPool->empty()) {
        return true;
    }
    if (!control->getWindow()) {
        return false;
    }
    XournalView* xournal = control->getWindow()->getXournal();
    Document* doc = control->getDocument();
    doc->lock_shared();
    const size_t pageNr = doc->indexOf(page);
    doc->unlock_shared();
    XojPageView* view = xournal ? xournal->getViewFor(pageNr) : nullptr;
    if (!view || view->getPage() != page) {
        return false;
    }
    view->addOverlayView(std::make_unique<xoj::view::HoldSnapView>(this, view));
    return true;
}

void StrokeHandler::showHoldRing() {
    if (!stroke || snapPending || stroke->getPointCount() < 2) {
        return;
    }
    // only where holding would straighten the stroke: a pause in the middle of a letter shows nothing
    ShapeRecognizer reco;
    if (!reco.recognizePatterns(stroke.get(), control->getSettings()->getStrokeRecognizerMinSize()) ||
        !ensureSnapView()) {
        return;
    }
    snapPool->dispatch(xoj::view::HoldSnapView::SHOW_RING_REQUEST, holdX, holdY);
}

void StrokeHandler::holdToShape() {
    if (!stroke || stroke->getPointCount() < 2) {
        return;
    }
    ShapeRecognizer reco;
    auto recognized = reco.recognizePatterns(stroke.get(), control->getSettings()->getStrokeRecognizerMinSize());
    if (!recognized) {
        return;  // not a shape: the stroke goes on as drawn
    }
    finalizeStroke(1.0);
    Layer* layer = page->getSelectedLayer();

    // with the animations off, or with no view of the page to draw on, it happens at once
    bool animate = control->getWindow() &&
                   !ambitus::motion::reducedMotion(control->getWindow()->getXournal()->getWidget()) && ensureSnapView();
    if (!animate) {
        // as on release with the shape recognizer: Ctrl+Z gives the hand-drawn stroke back
        control->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, stroke.get()));
        strokeRecognizerDetected(std::move(recognized), layer);  // the rest of the gesture finds no stroke
        return;
    }

    // Cross-fade: the hand-drawn stroke goes out in 120 ms and the shape comes in in 180 ms, on an overlay of the page.
    // The shape is only put in the page when the fade ends, so that it does not show whole under its own fade.
    prepareRecognized(recognized.get());
    snapShape = std::move(recognized);
    viewPool->dispatchAndClear(xoj::view::StrokeToolView::CANCELLATION_REQUEST, Range(stroke->getBoundingBox()));
    snapPool->dispatch(xoj::view::HoldSnapView::START_FADE_REQUEST, *stroke, *snapShape);
    snapPending = true;
    snapStart = g_get_monotonic_time();
    snapSource = g_timeout_add(
            16,
            +[](gpointer self) -> gboolean {
                static_cast<StrokeHandler*>(self)->snapTick();
                return static_cast<StrokeHandler*>(self)->snapSource ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
            },
            this);
}

void StrokeHandler::snapTick() {
    if (g_get_monotonic_time() - snapStart >= ambitus::motion::BASE_US) {
        snapSource = 0;  // this callback ends right after: the source goes with it
        commitSnap();
        return;
    }
    snapPool->dispatch(xoj::view::HoldSnapView::TICK_REQUEST);
}

void StrokeHandler::commitSnap() {
    if (!snapPending) {
        return;
    }
    snapPending = false;
    if (snapSource) {
        g_source_remove(snapSource);
        snapSource = 0;
    }
    Layer* layer = page->getSelectedLayer();
    // as on release with the shape recognizer: Ctrl+Z gives the hand-drawn stroke back
    control->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, stroke.get()));
    finishRecognized(std::move(snapShape), layer);  // the rest of the gesture finds no stroke
}

void StrokeHandler::paintTo(Point point) {
    if (this->hasPressure && point.z > 0.0) {
        point.z *= this->stroke->getWidth();
    }

    size_t pointCount = stroke->getPointCount();

    if (pointCount > 0) {
        Point endPoint = stroke->getPoint(pointCount - 1);
        double distance = point.lineLengthTo(endPoint);
        if (distance < PIXEL_MOTION_THRESHOLD) {  //(!validMotion(point, endPoint)) {
            if (pointCount == 1 && this->hasPressure && endPoint.z < point.z) {
                // Record the possible increase in pressure for the first point
                this->stroke->setLastPressure(point.z);
                this->viewPool->dispatch(xoj::view::StrokeToolView::THICKEN_FIRST_POINT_REQUEST, point.z);
            }
            return;
        }
        if (this->hasPressure) {
            /**
             * Both device and tool are pressure sensitive
             */
            if (const double widthDelta = point.z - endPoint.z;
                - widthDelta > MAX_WIDTH_VARIATION || widthDelta > MAX_WIDTH_VARIATION) {
                /**
                 * If the width variation is to big, decompose into shorter segments.
                 * Those segments can not be shorter than PIXEL_MOTION_THRESHOLD
                 */
                double nbSteps = std::min(std::ceil(std::abs(widthDelta) / MAX_WIDTH_VARIATION),
                                          std::floor(distance / PIXEL_MOTION_THRESHOLD));
                double stepLength = 1.0 / nbSteps;
                Point increment((point.x - endPoint.x) * stepLength, (point.y - endPoint.y) * stepLength,
                                widthDelta * stepLength);
                endPoint.z += increment.z;

                for (int i = 1; i < static_cast<int>(nbSteps); i++) {  // The last step is done below
                    endPoint.x += increment.x;
                    endPoint.y += increment.y;
                    endPoint.z += increment.z;
                    drawSegmentTo(endPoint);
                }
            }
        }
    }
    drawSegmentTo(point);
}

void StrokeHandler::drawSegmentTo(const Point& point) {

    this->stroke->addPoint(this->hasPressure ? point : Point(point.x, point.y));
    this->viewPool->dispatch(xoj::view::StrokeToolView::ADD_POINT_REQUEST, this->stroke->getPointVector().back());
    return;
}

void StrokeHandler::onSequenceCancelEvent() {
    stopHold();
    commitSnap();  // the shape was already on the screen: it stays, as it did when it went in at once
    if (this->stroke) {
        this->viewPool->dispatchAndClear(xoj::view::StrokeToolView::CANCELLATION_REQUEST,
                                         Range(this->stroke->getBoundingBox()));
        stroke.reset();
    }
}

void StrokeHandler::finalizeStroke(double pressure) {
    if (!stroke) {
        return;
    }

    /**
     * The stabilizer may have added a gap between the end of the stroke and the input device
     * Fill this gap.
     */
    stabilizer->finalizeStroke();

    // Backward compatibility and also easier to handle for me;-)
    // I cannot draw a line with one point, to draw a visible line I need two points,
    // twice the same Point is also OK
    if (auto const& pv = stroke->getPointVector(); pv.size() == 1) {
        const Point pt = pv.front();  // Make a copy, otherwise stroke->addPoint(pt); in UB
        if (this->hasPressure) {
            // Pressure inference provides a pressure value to the last event. Most devices set this value to 0.
            const double newPressure = std::max(pt.z, pressure * this->stroke->getWidth());
            this->stroke->setLastPressure(newPressure);
            this->viewPool->dispatch(xoj::view::StrokeToolView::THICKEN_FIRST_POINT_REQUEST, newPressure);
        }
        stroke->addPoint(pt);
    }

    stroke->freeUnusedPointItems();
}

void StrokeHandler::onButtonReleaseEvent(const PositionInputData& pos, double zoom) {
    stopHold();
    if (snapPending) {
        commitSnap();  // released before the fade ended: the shape goes in now
        return;
    }
    if (!stroke) {
        return;
    }
    finalizeStroke(pos.pressure);

    Layer* layer = page->getSelectedLayer();

    if (scratchOut(layer)) {
        return;
    }

    UndoRedoHandler* undo = control->getUndoRedoHandler();
    undo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, stroke.get()));

    Settings* settings = control->getSettings();
    if (settings->getEmptyLastPageAppend() == EmptyLastPageAppendType::OnDrawOfLastPage) {
        auto* doc = control->getDocument();
        doc->lock_shared();
        auto pdfPageCount = doc->getPdfPageCount();
        auto lastPage = doc->getPageCount() - 1;
        doc->unlock_shared();
        if (pdfPageCount == 0) {
            auto currentPage = control->getCurrentPageNo();
            if (currentPage == lastPage) {
                control->insertNewPage(currentPage + 1, true);
            }
        }
    }

    ToolHandler* h = control->getToolHandler();
    if (h->getDrawingType() == DRAWING_TYPE_SHAPE_RECOGNIZER) {
        ShapeRecognizer reco;

        auto recognized = reco.recognizePatterns(stroke.get(), control->getSettings()->getStrokeRecognizerMinSize());

        if (recognized) {
            // strokeRecognizerDetected handles the repainting and the deletion of the views.
            strokeRecognizerDetected(std::move(recognized), layer);
            return;
        }
    }

    auto ptr = stroke.get();
    Document* doc = control->getDocument();
    doc->lock();
    layer->addElement(std::move(stroke));
    doc->unlock();

    // Blitt the stroke to the page's buffer and delete all views.
    // Passing the empty Range() as no actual redrawing is necessary at this point
    this->viewPool->dispatchAndClear(xoj::view::StrokeToolView::FINALIZATION_REQUEST, Range());

    page->fireElementChanged(ptr);
}

bool StrokeHandler::scratchOut(Layer* layer) {
    if (!control->getSettings()->getScratchOutEnabled() || stroke->getToolType() != StrokeTool::PEN ||
        control->getToolHandler()->getDrawingType() != DRAWING_TYPE_DEFAULT ||
        static_cast<double>(g_get_monotonic_time() - startedAt) / G_USEC_PER_SEC > ambitus::scratch::MAX_DURATION) {
        return false;
    }
    using ambitus::scratch::Pt;
    auto toPts = [](const Stroke& s) {
        std::vector<Pt> pts;
        pts.reserve(s.getPointVector().size());
        for (auto const& p: s.getPointVector()) {
            pts.emplace_back(p.x, p.y);
        }
        return pts;
    };
    const auto scribble = toPts(*stroke);
    ambitus::scratch::Scribble shape;
    if (!ambitus::scratch::detectScribble(scribble, shape)) {
        return false;
    }

    const Range box(stroke->getBoundingBox());
    std::vector<const Element*> victims;
    for (auto const* e: layer->getElementsView()) {
        if (e->getType() != ELEMENT_STROKE || !box.intersect(Range(e->getBoundingBox())).isValid()) {
            continue;
        }
        auto const* s = static_cast<const Stroke*>(e);
        double tolerance = std::max(3.0, 0.5 * (stroke->getWidth() + s->getWidth()));
        if (ambitus::scratch::crossesOut(shape, scribble, toPts(*s), tolerance)) {
            victims.push_back(e);
        }
    }
    if (victims.empty()) {
        return false;  // nothing under it: the zigzag stays as ink
    }

    auto del = std::make_unique<DeleteUndoAction>(page, true);
    Range range = box;
    Document* doc = control->getDocument();
    doc->lock();
    for (auto const* v: victims) {
        range = range.unite(Range(v->getBoundingBox()));
        auto [ptr, pos] = layer->removeElement(v);
        if (pos != Element::InvalidIndex) {
            del->addElement(layer, std::move(ptr), pos);
        }
    }
    doc->unlock();
    control->getUndoRedoHandler()->addUndoAction(std::move(del));

    this->viewPool->dispatchAndClear(xoj::view::StrokeToolView::CANCELLATION_REQUEST, box);
    stroke.reset();
    page->fireRangeChanged(range);
    return true;
}

void StrokeHandler::strokeRecognizerDetected(std::unique_ptr<Stroke> recognized, Layer* layer) {
    prepareRecognized(recognized.get());
    finishRecognized(std::move(recognized), layer);
}

void StrokeHandler::prepareRecognized(Stroke* recognized) {
    recognized->setWidth(stroke->hasPressure() ? stroke->getAvgPressure() : stroke->getWidth());

    // snapping
    if (control->getSettings()->getSnapRecognizedShapesEnabled()) {
        Rectangle<double> oldSnappedBounds = recognized->getSnappedBounds();
        Point topLeft = Point(oldSnappedBounds.x, oldSnappedBounds.y);
        Point topLeftSnapped = snappingHandler.snapToGrid(topLeft, false);

        recognized->move(topLeftSnapped.x - topLeft.x, topLeftSnapped.y - topLeft.y);
        Rectangle<double> snappedBounds = recognized->getSnappedBounds();
        Point belowRight = Point(snappedBounds.x + snappedBounds.width, snappedBounds.y + snappedBounds.height);
        Point belowRightSnapped = snappingHandler.snapToGrid(belowRight, false);

        double fx = (std::abs(snappedBounds.width) > std::numeric_limits<double>::epsilon()) ?
                            (belowRightSnapped.x - topLeftSnapped.x) / snappedBounds.width :
                            1;
        double fy = (std::abs(snappedBounds.height) > std::numeric_limits<double>::epsilon()) ?
                            (belowRightSnapped.y - topLeftSnapped.y) / snappedBounds.height :
                            1;
        recognized->scale(topLeftSnapped.x, topLeftSnapped.y, fx, fy, 0, false);
    }
}

void StrokeHandler::finishRecognized(std::unique_ptr<Stroke> recognized, Layer* layer) {
    UndoRedoHandler* undo = control->getUndoRedoHandler();
    auto recognizedPtr = recognized.get();
    auto strokePtr = stroke.get();
    undo->addUndoAction(std::make_unique<RecognizerUndoAction>(page, layer, std::move(stroke), recognizedPtr));

    Document* doc = control->getDocument();
    doc->lock();
    layer->addElement(std::move(recognized));
    doc->unlock();

    Range range = Range(recognizedPtr->getBoundingBox()).unite(Range(strokePtr->getBoundingBox()));

    this->viewPool->dispatch(xoj::view::StrokeToolView::STROKE_REPLACEMENT_REQUEST, *recognizedPtr);

    // Blitt the new stroke to the page's buffer, delete all the views and refresh the area (so the recognized stroke
    // gets displayed instead of the old one).
    this->viewPool->dispatchAndClear(xoj::view::StrokeToolView::FINALIZATION_REQUEST, range);
    // Ambitus: the overlay of the fade draws the shape whole into the buffer, and goes away
    snapPool->dispatchAndClear(xoj::view::HoldSnapView::FINISH_REQUEST);
    page->fireElementChanged(recognizedPtr);
}

void StrokeHandler::onButtonPressEvent(const PositionInputData& pos, double zoom) {
    xoj_assert(!stroke);

    this->buttonDownPoint.x = pos.x / zoom;
    this->buttonDownPoint.y = pos.y / zoom;

    stroke = createStroke(this->control);
    this->startedAt = g_get_monotonic_time();

    this->hasPressure = this->stroke->getToolType().isPressureSensitive() && pos.pressure != Point::NO_PRESSURE;

    const double width = this->hasPressure ? pos.pressure * stroke->getWidth() : Point::NO_PRESSURE;
    stroke->addPoint(Point(this->buttonDownPoint.x, this->buttonDownPoint.y, width));

    stabilizer->initialize(this, zoom, pos);
    restartHold(pos.x, pos.y, zoom);
}

void StrokeHandler::onButtonDoublePressEvent(const PositionInputData&, double) {
    // nothing to do
}

auto StrokeHandler::createView(xoj::view::Repaintable* parent) const -> std::unique_ptr<xoj::view::OverlayView> {
    xoj_assert(this->stroke);
    const Stroke& s = *this->stroke;
    if (s.getFill() != -1) {
        if (s.getToolType() == StrokeTool::HIGHLIGHTER) {
            // Filled highlighter requires to wipe the mask entirely at every iteration
            // It has a dedicated view class.
            return std::make_unique<xoj::view::StrokeToolFilledHighlighterView>(this, s, parent);
        } else {
            return std::make_unique<xoj::view::StrokeToolFilledView>(this, s, parent);
        }
    } else {
        return std::make_unique<xoj::view::StrokeToolView>(this, s, parent);
    }
}

auto StrokeHandler::getViewPool() const -> const std::shared_ptr<xoj::util::DispatchPool<xoj::view::StrokeToolView>>& {
    return viewPool;
}

auto StrokeHandler::getSnapPool() const -> const std::shared_ptr<xoj::util::DispatchPool<xoj::view::HoldSnapView>>& {
    return snapPool;
}
