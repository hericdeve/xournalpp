#include "StrokeHandler.h"

#include <algorithm>  // for max, min
#include <cmath>      // for ceil, pow, abs
#include <limits>     // for numeric_limits
#include <memory>     // for unique_ptr, mak...
#include <utility>    // for move
#include <vector>     // for vector

#include <gdk/gdk.h>  // for GdkEventKey

#include "control/Control.h"                                // for Control
#include "control/ToolEnums.h"                              // for DRAWING_TYPE_ST...
#include "control/ToolHandler.h"                            // for ToolHandler
#include "control/layer/LayerController.h"                  // for LayerController
#include "control/settings/Settings.h"                      // for Settings
#include "control/settings/SettingsEnums.h"                 // for EmptyLastPageAppendType
#include "control/shaperecognizer/ShapeRecognizer.h"        // for ShapeRecognizer
#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "control/tools/InputHandler.h"                     // for InputHandler::P...
#include "control/tools/SnapToGridInputHandler.h"           // for SnapToGridInput...
#include "gui/inputdevices/PositionInputData.h"             // for PositionInputData
#include "model/Document.h"                                 // for Document
#include "model/Element.h"
#include "model/Layer.h"                                    // for Layer
#include "model/LineStyle.h"                                // for LineStyle
#include "model/Stroke.h"                                   // for Stroke, STROKE_...
#include "model/XojPage.h"                                  // for XojPage
#include "undo/InsertUndoAction.h"                          // for InsertUndoAction
#include "undo/RecognizerUndoAction.h"                      // for RecognizerUndoA...
#include "undo/UndoRedoHandler.h"                           // for UndoRedoHandler
#include "util/Assert.h"                                    // for xoj_assert
#include "util/DispatchPool.h"                              // for DispatchPool
#include "util/Range.h"                                     // for Range
#include "util/Rectangle.h"                                 // for Rectangle, util
#include "util/glib_casts.h"                                // for wrap_for_once_v
#include "view/overlays/StrokeToolFilledHighlighterView.h"  // for StrokeToolFilledHighlighterView
#include "view/overlays/StrokeToolFilledView.h"             // for StrokeToolFilledView
#include "view/overlays/StrokeToolView.h"                   // for StrokeToolView

#include "StrokeStabilizer.h"  // for Base, get

using xoj::util::Rectangle;

StrokeHandler::StrokeHandler(Control* control, const PageRef& page):
        InputHandler(control, page),
        snappingHandler(control->getSettings()),
        stabilizer(StrokeStabilizer::get(control->getSettings())),
        viewPool(std::make_shared<xoj::util::DispatchPool<xoj::view::StrokeToolView>>()) {
    snappingHandler.setPageRef(page);
}

StrokeHandler::~StrokeHandler() {
    cancelHoldTimer();
}

auto StrokeHandler::onKeyPressEvent(const KeyEvent&) -> bool { return false; }
auto StrokeHandler::onKeyReleaseEvent(const KeyEvent&) -> bool { return false; }

auto StrokeHandler::onMotionNotifyEvent(const PositionInputData& pos, double zoom) -> bool {
    if (!stroke) {
        return false;
    }

    if (pos.pressure == 0) {
        /**
         * Some devices emit a move event with pressure 0 when lifting the stylus tip
         * Ignore those events
         */
        return true;
    }

    Point currentPoint(pos.x / zoom, pos.y / zoom);

    if (this->isHoldShapeRecognized) {
        handleHoldMotion(currentPoint);
        return true;
    }

    // Normal drawing mode: track stationary hold
    ToolHandler* h = control->getToolHandler();
    Settings* settings = control->getSettings();
    bool canDrawAndHold = settings->getDrawAndHoldEnabled() &&
                          (h->getToolType() == TOOL_PEN || h->getToolType() == TOOL_HIGHLIGHTER) &&
                          (h->getDrawingType() == DRAWING_TYPE_DEFAULT || h->getDrawingType() == DRAWING_TYPE_SHAPE_RECOGNIZER);

    if (canDrawAndHold) {
        if (currentPoint.lineLengthTo(this->holdAnchorPoint) > 8.0) {
            this->holdAnchorPoint = currentPoint;
            this->holdTimer = g_timeout_add(settings->getDrawAndHoldTimeout(),
                                            xoj::util::wrap_for_once_v<onHoldTimeout>, this);
        }
    }

    stabilizer->processEvent(pos);
    return true;
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

void StrokeHandler::cancelHoldTimer() {
    this->holdTimer.cancel();
}

auto StrokeHandler::onHoldTimeout(StrokeHandler* self) -> bool {
    self->holdTimer.consume();
    self->triggerHoldShapeRecognition();
    return false;
}

void StrokeHandler::triggerHoldShapeRecognition() {
    if (!this->stroke || this->isHoldShapeRecognized) {
        return;
    }

    if (this->stroke->getPointCount() < 4) {
        return;
    }

    ShapeRecognizer reco;
    auto recognized = reco.recognizePatterns(this->stroke.get(), this->control->getSettings()->getStrokeRecognizerMinSize());

    bool isCircuit = false;
    const xoj::circuit::CircuitTemplate* circuitTpl = nullptr;
    Point cTermStart{0.0, 0.0};
    Point cTermEnd{0.0, 0.0};

    if (!recognized && this->control->getCustomShapeManager()) {
        xoj::circuit::CircuitRecognitionResult cRes;
        recognized = this->control->getCustomShapeManager()->recognize(this->stroke.get(), &cRes);
        if (recognized) {
            isCircuit = true;
            circuitTpl = cRes.matchedTemplate;
            cTermStart = cRes.terminalStart;
            cTermEnd = cRes.terminalEnd;
        }
    }

    if (!recognized) {
        return;
    }

    recognized->setWidth(this->stroke->hasPressure() ? this->stroke->getAvgPressure() : this->stroke->getWidth());

    // Snapping if enabled
    if (!isCircuit && this->control->getSettings()->getSnapRecognizedShapesEnabled()) {
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
        bool restoreLineWidth = this->control->getSettings()->getRestoreLineWidthEnabled();
        recognized->scale(topLeftSnapped.x, topLeftSnapped.y, fx, fy, 0, restoreLineWidth);
    }

    this->isHoldShapeRecognized = true;
    this->originalStroke = this->stroke->cloneStroke();
    this->baseRecognizedStroke = std::move(recognized);
    this->currentRecognizedStroke = this->baseRecognizedStroke->cloneStroke();
    this->holdSnapPoint = this->holdAnchorPoint;

    // Determine shape type and center
    if (isCircuit) {
        this->recognizedType = RecognizedShapeType::Circuit;
        this->recognizedCircuitTemplate = circuitTpl;
        this->circuitTerminalStart = cTermStart;
        this->circuitTerminalEnd = cTermEnd;
    } else {
        this->recognizedCircuitTemplate = nullptr;
        const auto& pts = this->baseRecognizedStroke->getPointVector();
        if (pts.size() == 2) {
            this->recognizedType = RecognizedShapeType::Line;
            this->shapeCenter = pts.front();
        } else if (pts.size() >= 24 && pts.front().lineLengthTo(pts.back()) < 1.0) {
            this->recognizedType = RecognizedShapeType::Circle;
            Rectangle<double> bbox = this->baseRecognizedStroke->getBoundingBox();
            this->shapeCenter = Point(bbox.x + bbox.width / 2.0, bbox.y + bbox.height / 2.0);
        } else {
            this->recognizedType = RecognizedShapeType::Polygon;
            Rectangle<double> bbox = this->baseRecognizedStroke->getBoundingBox();
            this->shapeCenter = Point(bbox.x + bbox.width / 2.0, bbox.y + bbox.height / 2.0);
        }
    }

    Range dirtyRange = Range(this->stroke->getBoundingBox()).unite(Range(this->currentRecognizedStroke->getBoundingBox()));
    dirtyRange.addPadding(this->stroke->getWidth() + 2.0);
    this->currentOverlayRange = dirtyRange;

    this->viewPool->dispatch(xoj::view::StrokeToolView::STROKE_REPLACEMENT_REQUEST, *this->currentRecognizedStroke, dirtyRange);
}

void StrokeHandler::handleHoldMotion(const Point& currentPoint) {
    if (!this->isHoldShapeRecognized || !this->baseRecognizedStroke) {
        return;
    }

    if (!this->control->getSettings()->getDrawAndHoldResizeEnabled()) {
        return;
    }

    auto updated = this->baseRecognizedStroke->cloneStroke();

    if (this->recognizedType == RecognizedShapeType::Circuit && this->recognizedCircuitTemplate) {
        // Circuit: end terminal tracks currentPoint with dynamic snapping & lead stretching
        auto customMgr = this->control->getCustomShapeManager();
        if (customMgr) {
            auto snappedCircuit = customMgr->snapShape(this->recognizedCircuitTemplate, this->circuitTerminalStart,
                                                      currentPoint, this->baseRecognizedStroke.get(), true, &this->snappingHandler);
            if (snappedCircuit) {
                updated = std::move(snappedCircuit);
            }
        }
    } else if (this->recognizedType == RecognizedShapeType::Line) {
        // Line: start point is fixed at p0, end point tracks currentPoint
        const auto& basePts = this->baseRecognizedStroke->getPointVector();
        if (basePts.size() == 2) {
            Point p0 = basePts.front();
            Point p1 = snappingHandler.snap(currentPoint, p0, false);
            auto newStroke = std::make_unique<Stroke>();
            newStroke->applyStyleFrom(this->baseRecognizedStroke.get());
            newStroke->addPoint(p0);
            newStroke->addPoint(p1);
            updated = std::move(newStroke);
        }
    } else {
        // Circle / Polygon: scale relative to center based on distance ratio from snap point
        double initialDist = this->holdSnapPoint.lineLengthTo(this->shapeCenter);
        double currentDist = currentPoint.lineLengthTo(this->shapeCenter);

        if (initialDist > 5.0 && currentDist > 5.0) {
            double scale = currentDist / initialDist;
            if (scale > 0.05 && scale < 20.0) {
                bool restoreLineWidth = this->control->getSettings()->getRestoreLineWidthEnabled();
                updated->scale(this->shapeCenter.x, this->shapeCenter.y, scale, scale, 0, restoreLineWidth);
            }
        }
    }

    Range oldBbox(this->currentRecognizedStroke->getBoundingBox());
    Range newBbox(updated->getBoundingBox());
    Range dirtyRange = oldBbox.unite(newBbox).unite(this->currentOverlayRange);
    dirtyRange.addPadding(updated->getWidth() + 2.0);
    this->currentOverlayRange = Range(updated->getBoundingBox());
    this->currentOverlayRange.addPadding(updated->getWidth() + 2.0);

    this->currentRecognizedStroke = std::move(updated);
    this->viewPool->dispatch(xoj::view::StrokeToolView::STROKE_REPLACEMENT_REQUEST, *this->currentRecognizedStroke, dirtyRange);
}

void StrokeHandler::onSequenceCancelEvent() {
    cancelHoldTimer();
    if (this->stroke) {
        Range r(this->stroke->getBoundingBox());
        if (this->currentRecognizedStroke) {
            r = r.unite(Range(this->currentRecognizedStroke->getBoundingBox()));
        }
        r.addPadding(this->stroke->getWidth() + 2.0);
        this->viewPool->dispatchAndClear(xoj::view::StrokeToolView::CANCELLATION_REQUEST, r);
        stroke.reset();
        currentRecognizedStroke.reset();
        baseRecognizedStroke.reset();
        originalStroke.reset();
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
    cancelHoldTimer();
    if (!stroke) {
        return;
    }

    if (this->isHoldShapeRecognized && this->currentRecognizedStroke) {
        Layer* layer = page->getSelectedLayer();
        UndoRedoHandler* undo = control->getUndoRedoHandler();
        auto recognizedPtr = this->currentRecognizedStroke.get();
        auto originalPtr = this->originalStroke.get();

        undo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, originalPtr));
        undo->addUndoAction(std::make_unique<RecognizerUndoAction>(page, layer, std::move(this->originalStroke), recognizedPtr));

        Document* doc = control->getDocument();
        doc->lock();
        layer->addElement(std::move(this->currentRecognizedStroke));
        doc->unlock();

        Range range = Range(recognizedPtr->getBoundingBox()).unite(Range(originalPtr->getBoundingBox())).unite(this->currentOverlayRange);
        range.addPadding(recognizedPtr->getWidth() + 2.0);

        this->viewPool->dispatch(xoj::view::StrokeToolView::STROKE_REPLACEMENT_REQUEST, *recognizedPtr, range);
        this->viewPool->dispatchAndClear(xoj::view::StrokeToolView::FINALIZATION_REQUEST, range);
        page->fireElementChanged(recognizedPtr);

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

        stroke.reset();
        baseRecognizedStroke.reset();
        return;
    }

    finalizeStroke(pos.pressure);

    Layer* layer = page->getSelectedLayer();

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

        if (!recognized && control->getCustomShapeManager()) {
            recognized = control->getCustomShapeManager()->recognize(stroke.get());
        }

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

void StrokeHandler::strokeRecognizerDetected(std::unique_ptr<Stroke> recognized, Layer* layer) {
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
        bool restoreLineWidth = control->getSettings()->getRestoreLineWidthEnabled();
        recognized->scale(topLeftSnapped.x, topLeftSnapped.y, fx, fy, 0, restoreLineWidth);
    }

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
    page->fireElementChanged(recognizedPtr);
}

void StrokeHandler::onButtonPressEvent(const PositionInputData& pos, double zoom) {
    xoj_assert(!stroke);

    cancelHoldTimer();
    this->isHoldShapeRecognized = false;
    this->recognizedType = RecognizedShapeType::None;
    this->originalStroke.reset();
    this->baseRecognizedStroke.reset();
    this->currentRecognizedStroke.reset();
    this->currentOverlayRange = Range();

    this->buttonDownPoint.x = pos.x / zoom;
    this->buttonDownPoint.y = pos.y / zoom;
    this->holdAnchorPoint = this->buttonDownPoint;

    stroke = createStroke(this->control);

    ToolHandler* h = control->getToolHandler();
    Settings* settings = control->getSettings();
    bool canDrawAndHold = settings->getDrawAndHoldEnabled() &&
                          (h->getToolType() == TOOL_PEN || h->getToolType() == TOOL_HIGHLIGHTER) &&
                          (h->getDrawingType() == DRAWING_TYPE_DEFAULT || h->getDrawingType() == DRAWING_TYPE_SHAPE_RECOGNIZER);

    if (canDrawAndHold) {
        this->holdTimer = g_timeout_add(settings->getDrawAndHoldTimeout(),
                                        xoj::util::wrap_for_once_v<onHoldTimeout>, this);
    }

    this->hasPressure = this->stroke->getToolType().isPressureSensitive() && pos.pressure != Point::NO_PRESSURE;

    const double width = this->hasPressure ? pos.pressure * stroke->getWidth() : Point::NO_PRESSURE;
    stroke->addPoint(Point(this->buttonDownPoint.x, this->buttonDownPoint.y, width));

    stabilizer->initialize(this, zoom, pos);
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
