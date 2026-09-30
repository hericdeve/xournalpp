/*
 * Xournal++
 *
 * Auto Shape Selection Tool
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "AutoShapeSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <vector>

#include "CircuitDecomposer.h"
#include "CircuitFeatureClassifier.h"
#include "control/Control.h"
#include "control/shaperecognizer/ShapeRecognizer.h"
#include "control/shaperecognizer/custom/CircuitSnapper.h"
#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "control/shaperecognizer/image/CircuitSnipRecognizer.h"
#include "control/tools/EditSelection.h"
#include "gui/MainWindow.h"
#include "gui/PageView.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "model/ElementInsertionPosition.h"
#include "model/Layer.h"
#include "model/PageRef.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/DeleteUndoAction.h"
#include "undo/GroupUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/RecognizerUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Rectangle.h"
#include "util/i18n.h"

namespace xoj::circuit {

namespace {

/**
 * Snaps a stroke into a straight line if its points deviate minimally from the chord connecting its ends.
 */
auto snapStraightWire(const Stroke* stroke) -> std::unique_ptr<Stroke> {
    const size_t count = stroke->getPointCount();
    if (count < 2) {
        return nullptr;
    }

    // Protection: NEVER flatten a stroke that has alternating oscillations or resembles a component
    auto featClass = CircuitFeatureClassifier::classify(stroke);
    if (featClass == CircuitFeatureClass::ResistorIeee || featClass == CircuitFeatureClass::Inductor ||
        featClass == CircuitFeatureClass::SineWave) {
        return nullptr;
    }

    auto feat = CircuitFeatureClassifier::extractFeatures(stroke);
    if (feat.alternatingExtremaCount >= 2 || feat.positivePeakCount >= 2 || feat.negativeValleyCount >= 2) {
        return nullptr;
    }
    if (feat.hasOscillatingBody && feat.bodySinuosity > 1.08) {
        return nullptr;
    }

    const Point& p0 = stroke->getPoint(0);
    const Point& p1 = stroke->getPoint(count - 1);
    const double dx = p1.x - p0.x;
    const double dy = p1.y - p0.y;
    const double chordLen = std::hypot(dx, dy);

    if (chordLen < 15.0) {
        return nullptr;
    }

    // Check maximum perpendicular distance of points to the chord
    double maxPerpDist = 0.0;
    for (size_t i = 1; i + 1 < count; ++i) {
        const Point& pt = stroke->getPoint(i);
        const double perpDist = std::abs((p1.y - p0.y) * pt.x - (p1.x - p0.x) * pt.y + p1.x * p0.y - p1.y * p0.x) / chordLen;
        maxPerpDist = std::max(maxPerpDist, perpDist);
    }

    // Strict straight wire tolerance: true wires do not have large perpendicular waves
    if (maxPerpDist > std::min(6.0, chordLen * 0.06)) {
        return nullptr;
    }

    // Snap to 0 deg or 90 deg if close (Manhattan wire routing)
    double angle = std::atan2(dy, dx);
    const double snapThreshold = 25.0 * (M_PI / 180.0);
    Point endPt = p1;

    if (std::abs(angle) < snapThreshold || std::abs(std::abs(angle) - M_PI) < snapThreshold) {
        // Horizontal
        endPt.y = p0.y;
    } else if (std::abs(std::abs(angle) - M_PI / 2.0) < snapThreshold) {
        // Vertical
        endPt.x = p0.x;
    }

    auto out = std::make_unique<Stroke>(*stroke);
    std::vector<Point> pts;
    pts.push_back(p0);
    pts.push_back(endPt);
    out->setPointVector(std::move(pts));
    return out;
}

/**
 * Detects hand-drawn multi-stroke ground symbols:
 * Either:
 *   1. A vertical stem stroke with 2 or 3 short horizontal strokes beneath it, OR
 *   2. 2 to 4 descending horizontal bars stacked under an existing wire or component.
 */
struct MultiStrokeGround {
    Stroke* stem = nullptr;
    std::vector<Stroke*> bars;
    Point topPt{0.0, 0.0};
    Point bottomPt{0.0, 0.0};
};

auto detectMultiStrokeGrounds(const std::vector<Stroke*>& candidates) -> std::vector<MultiStrokeGround> {
    std::vector<MultiStrokeGround> grounds;
    std::set<Stroke*> consumed;

    // Phase 1: Stem + Bars
    for (Stroke* s1: candidates) {
        if (!s1 || consumed.count(s1) || s1->getPointCount() < 2) continue;
        const auto& pts1 = s1->getPointVector();
        double dx = pts1.back().x - pts1.front().x;
        double dy = pts1.back().y - pts1.front().y;
        double dist = std::hypot(dx, dy);

        // Check if s1 is a nearly vertical stem
        if (dist < 8.0 || dist > 120.0) continue;
        double angle = std::atan2(dy, dx);
        bool isDownwards = (std::abs(angle - M_PI / 2.0) < 0.40); // ~vertical down
        bool isUpwards = (std::abs(angle + M_PI / 2.0) < 0.40);   // ~vertical up
        if (!isDownwards && !isUpwards) continue;

        Point topP = isDownwards ? pts1.front() : pts1.back();
        Point botP = isDownwards ? pts1.back() : pts1.front();

        // Look for horizontal bars beneath botP
        std::vector<Stroke*> matchingBars;
        for (Stroke* s2: candidates) {
            if (s2 == s1 || consumed.count(s2) || s2->getPointCount() < 2) continue;
            const auto& pts2 = s2->getPointVector();
            double bx = pts2.back().x - pts2.front().x;
            double by = pts2.back().y - pts2.front().y;
            double bLen = std::hypot(bx, by);

            if (bLen < 4.0 || bLen > 65.0) continue;
            double bAngle = std::atan2(by, bx);
            // Must be roughly horizontal (angle near 0 or PI)
            if (std::abs(bAngle) > 0.45 && std::abs(std::abs(bAngle) - M_PI) > 0.45) continue;

            Point barCenter((pts2.front().x + pts2.back().x) * 0.5, (pts2.front().y + pts2.back().y) * 0.5);
            if (std::abs(barCenter.x - botP.x) > 24.0) continue;

            double vertDist = barCenter.y - botP.y;
            if (vertDist >= -8.0 && vertDist <= 45.0) {
                matchingBars.push_back(s2);
            }
        }

        if (matchingBars.size() >= 2 && matchingBars.size() <= 4) {
            std::sort(matchingBars.begin(), matchingBars.end(), [](Stroke* a, Stroke* b) {
                return a->getBoundingBox().y < b->getBoundingBox().y;
            });

            MultiStrokeGround g;
            g.stem = s1;
            g.bars = matchingBars;
            g.topPt = topP;
            g.bottomPt = Point(botP.x, matchingBars.back()->getBoundingBox().y + matchingBars.back()->getBoundingBox().height);
            grounds.push_back(g);

            consumed.insert(s1);
            for (auto* b: matchingBars) {
                consumed.insert(b);
            }
        }
    }

    // Phase 2: Direct horizontal bar stack without an explicit separate stem stroke
    std::vector<Stroke*> remainingHori;
    for (Stroke* s: candidates) {
        if (!s || consumed.count(s) || s->getPointCount() < 2) continue;
        // Never classify handwriting as ground bars
        if (CircuitFeatureClassifier::isHandwritingOrAnnotation(s)) continue;

        const auto& pts = s->getPointVector();
        double dx = pts.back().x - pts.front().x;
        double dy = pts.back().y - pts.front().y;
        double len = std::hypot(dx, dy);
        if (len >= 4.0 && len <= 35.0) {
            double angle = std::atan2(dy, dx);
            if (std::abs(angle) < 0.35 || std::abs(std::abs(angle) - M_PI) < 0.35) {
                remainingHori.push_back(s);
            }
        }
    }

    std::sort(remainingHori.begin(), remainingHori.end(), [](Stroke* a, Stroke* b) {
        return a->getBoundingBox().y < b->getBoundingBox().y;
    });

    for (size_t i = 0; i < remainingHori.size(); ++i) {
        Stroke* bTop = remainingHori[i];
        if (consumed.count(bTop)) continue;

        auto boxTop = bTop->getBoundingBox();
        double centerX = boxTop.x + boxTop.width * 0.5;
        std::vector<Stroke*> stack;
        stack.push_back(bTop);

        for (size_t j = i + 1; j < remainingHori.size(); ++j) {
            Stroke* bNext = remainingHori[j];
            if (consumed.count(bNext)) continue;

            auto boxNext = bNext->getBoundingBox();
            double nextCenterX = boxNext.x + boxNext.width * 0.5;
            double dy = boxNext.y - stack.back()->getBoundingBox().y;

            if (std::abs(nextCenterX - centerX) <= 16.0 && dy > 2.0 && dy <= 22.0) {
                stack.push_back(bNext);
                if (stack.size() == 3) break;
            }
        }

        if (stack.size() >= 2) {
            MultiStrokeGround g;
            g.stem = nullptr;
            g.bars = stack;
            g.topPt = Point(centerX, stack.front()->getBoundingBox().y);
            g.bottomPt = Point(centerX, stack.back()->getBoundingBox().y + stack.back()->getBoundingBox().height);
            grounds.push_back(g);

            for (auto* b: stack) {
                consumed.insert(b);
            }
        }
    }

    return grounds;
}

/**
 * Snaps wire endpoints that meet perpendicular straight rails into clean 90-degree T-junctions,
 * extending the rail slightly if needed to guarantee physical contact.
 */
void applyTJunctionSnapping(const std::vector<Element*>& newlyShapedElements) {
    struct RailSegment {
        Stroke* stroke;
        size_t segIdx;
        Point p0;
        Point p1;
        bool isHorizontal;
        bool isVertical;
    };

    std::vector<RailSegment> rails;

    for (Element* elem: newlyShapedElements) {
        auto* s = static_cast<Stroke*>(elem);
        // Only consider simple 2-point wire segments as rails
        if (s->getPointCount() != 2) continue;

        const auto& pts = s->getPointVector();
        Point a = pts[0];
        Point b = pts[1];
        double dx = std::abs(b.x - a.x);
        double dy = std::abs(b.y - a.y);
        double len = std::hypot(b.x - a.x, b.y - a.y);

        if (len >= 25.0) {
            if (dy < 1.0) {
                rails.push_back({s, 0, a, b, true, false});
            } else if (dx < 1.0) {
                rails.push_back({s, 0, a, b, false, true});
            }
        }
    }

    if (rails.empty()) {
        return;
    }

    for (Element* elem: newlyShapedElements) {
        auto* s = static_cast<Stroke*>(elem);
        // Do not alter complex composite or multi-stroke components
        if (s->getPointCount() > 2) continue;

        auto pts = s->getPointVector();
        if (pts.size() != 2) continue;

        bool modified = false;

        for (int endIdx = 0; endIdx < 2; ++endIdx) {
            Point& pt = (endIdx == 0) ? pts.front() : pts.back();
            Point otherPt = (endIdx == 0) ? pts[1] : pts[pts.size() - 2];

            double wireDx = std::abs(pt.x - otherPt.x);
            double wireDy = std::abs(pt.y - otherPt.y);
            bool wireIsVertical = (wireDx < 4.0 && wireDy >= 5.0);
            bool wireIsHorizontal = (wireDy < 4.0 && wireDx >= 5.0);

            for (auto& rail: rails) {
                if (rail.stroke == s) continue;

                if (wireIsVertical && rail.isHorizontal) {
                    double railY = rail.p0.y;
                    double minX = std::min(rail.p0.x, rail.p1.x);
                    double maxX = std::max(rail.p0.x, rail.p1.x);

                    if (std::abs(pt.y - railY) <= 18.0 && pt.x >= minX - 10.0 && pt.x <= maxX + 10.0) {
                        pt.y = railY;
                        modified = true;
                        break;
                    }
                } else if (wireIsHorizontal && rail.isVertical) {
                    double railX = rail.p0.x;
                    double minY = std::min(rail.p0.y, rail.p1.y);
                    double maxY = std::max(rail.p0.y, rail.p1.y);

                    if (std::abs(pt.x - railX) <= 18.0 && pt.y >= minY - 10.0 && pt.y <= maxY + 10.0) {
                        pt.x = railX;
                        modified = true;
                        break;
                    }
                }
            }
        }

        if (modified) {
            s->setPointVector(std::move(pts));
        }
    }
}
/**
 * Snaps an interior stroke inside an AC source circle into a clean mathematical sine wave.
 */
auto snapSineWave(const Stroke* stroke, const Point& circleCenter, double circleRadius) -> std::unique_ptr<Stroke> {
    if (!stroke || stroke->getPointCount() < 4) return nullptr;

    auto featClass = CircuitFeatureClassifier::classify(stroke);
    auto feat = CircuitFeatureClassifier::extractFeatures(stroke);

    if (featClass != CircuitFeatureClass::SineWave &&
        !(feat.positivePeakCount >= 1 && feat.negativeValleyCount >= 1 && feat.alternatingExtremaCount <= 3)) {
        return nullptr;
    }

    auto box = stroke->getBoundingBox();
    Point strokeCenter(box.x + box.width * 0.5, box.y + box.height * 0.5);

    // Verify it is inside or near the circle center
    if (strokeCenter.lineLengthTo(circleCenter) > circleRadius * 0.60) {
        return nullptr;
    }

    double waveWidth = std::clamp(circleRadius * 1.1, 10.0, circleRadius * 1.6);
    double waveHeight = std::clamp(circleRadius * 0.45, 4.0, circleRadius * 0.70);

    auto out = std::make_unique<Stroke>(*stroke);
    std::vector<Point> pts;
    constexpr int NUM_PTS = 25;
    for (int i = 0; i <= NUM_PTS; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(NUM_PTS);
        double x = (circleCenter.x - waveWidth * 0.5) + t * waveWidth;
        double y = circleCenter.y - std::sin(t * 2.0 * M_PI) * waveHeight;
        pts.emplace_back(x, y);
    }
    out->setPointVector(std::move(pts));
    return out;
}

/**
 * Snaps an L-shaped corner wire into two orthogonal perpendicular segments.
 */
auto snapCornerWire(const Stroke* stroke) -> std::unique_ptr<Stroke> {
    const size_t count = stroke->getPointCount();
    if (count < 8) {
        return nullptr;
    }

    const Point& p0 = stroke->getPoint(0);
    const Point& p1 = stroke->getPoint(count - 1);
    const double chordLen = std::hypot(p1.x - p0.x, p1.y - p0.y);
    if (chordLen < 25.0) {
        return nullptr;
    }

    // Find point of maximum perpendicular distance from the start-to-end chord
    double maxDist = 0.0;
    size_t cornerIdx = 0;
    for (size_t i = 2; i + 2 < count; ++i) {
        const Point& pt = stroke->getPoint(i);
        const double d = std::abs((p1.y - p0.y) * pt.x - (p1.x - p0.x) * pt.y + p1.x * p0.y - p1.y * p0.x) / chordLen;
        if (d > maxDist) {
            maxDist = d;
            cornerIdx = i;
        }
    }

    // A true corner has significant deviation from chord
    if (maxDist < 12.0 || cornerIdx < 3 || cornerIdx + 3 >= count) {
        return nullptr;
    }

    // Candidate corner point
    const Point& pc = stroke->getPoint(cornerIdx);

    // Form orthogonal corner: either (p0.x, p1.y) or (p1.x, p0.y)
    Point cornerA(p0.x, p1.y);
    Point cornerB(p1.x, p0.y);
    const double distA = std::hypot(pc.x - cornerA.x, pc.y - cornerA.y);
    const double distB = std::hypot(pc.x - cornerB.x, pc.y - cornerB.y);

    Point chosenCorner = (distA < distB) ? cornerA : cornerB;
    const double bestDist = std::min(distA, distB);

    if (bestDist > std::min(24.0, chordLen * 0.25)) {
        return nullptr;
    }

    auto out = std::make_unique<Stroke>(*stroke);
    std::vector<Point> pts;
    pts.push_back(p0);
    pts.push_back(chosenCorner);
    pts.push_back(p1);
    out->setPointVector(std::move(pts));
    return out;
}

}  // namespace

auto AutoShapeSelection::autoShapeSelectedContent(Control* control) -> bool {
    if (!control || !control->getWindow() || !control->getWindow()->getXournal()) {
        return false;
    }

    EditSelection* selection = control->getWindow()->getXournal()->getSelection();
    if (!selection) {
        return false;
    }

    PageRef page = selection->getSourcePage();
    Layer* layer = selection->getSourceLayer();
    XojPageView* view = selection->getView();
    if (!page || !layer || !view) {
        return false;
    }

    CustomShapeManager* customMgr = control->getCustomShapeManager();

    // Identify which elements belong to the selection BEFORE clearing it
    std::vector<const Element*> selectedElementPtrs;
    for (const Element* e: selection->getElementsView()) {
        if (e && e->getType() == ELEMENT_STROKE) {
            selectedElementPtrs.push_back(e);
        }
    }

    if (selectedElementPtrs.empty()) {
        return false;
    }

    // Fast check: verify at least one stroke can be recognized before clearing selection
    bool anyRecognizable = false;
    std::vector<Stroke*> candidateStrokes;
    for (const Element* e: selectedElementPtrs) {
        candidateStrokes.push_back(const_cast<Stroke*>(static_cast<const Stroke*>(e)));
    }

    if (!detectMultiStrokeGrounds(candidateStrokes).empty()) {
        anyRecognizable = true;
    }

    if (!anyRecognizable) {
        for (Stroke* s: candidateStrokes) {
            if (s->getPointCount() < 2) {
                continue;
            }
            if (customMgr && customMgr->recognize(s, nullptr, 0.60)) {
                anyRecognizable = true;
                break;
            }
            ShapeRecognizer standardRecognizer;
            if (standardRecognizer.recognizePatterns(s, 10.0)) {
                anyRecognizable = true;
                break;
            }
            if (snapCornerWire(s) || snapStraightWire(s)) {
                anyRecognizable = true;
                break;
            }
        }
    }

    if (!anyRecognizable) {
        return false;
    }

    // 1. Finalize the active selection so all elements are placed back in the layer
    //    with their latest translated/modified positions and known indices.
    control->getWindow()->getXournal()->clearSelection();

    Document* doc = control->getDocument();
    if (!doc) {
        return false;
    }

    // Match candidates in the layer that correspond to the strokes that were in the selection
    std::vector<Stroke*> candidates;
    for (const auto& elem: layer->getElements()) {
        if (elem && elem->getType() == ELEMENT_STROKE) {
            auto it = std::find(selectedElementPtrs.begin(), selectedElementPtrs.end(), elem.get());
            if (it != selectedElementPtrs.end()) {
                candidates.push_back(static_cast<Stroke*>(elem.get()));
            }
        }
    }

    if (candidates.empty()) {
        return false;
    }

    auto groupUndo = std::make_unique<GroupUndoAction>();
    std::vector<Element*> newlyShapedElements;
    std::set<Stroke*> consumedStrokes;

    // 0. Primary Snip Recognition Pipeline: 2D Image Skeleton & Topology Extraction
    // When multiple strokes are selected, analyze the 2D image as a whole to avoid
    // stroke-order, pen-lift, or compound stroke artifacts.
    if (customMgr && candidates.size() >= 3) {
        auto snipResult = CircuitSnipRecognizer::processSnip(candidates, customMgr);
        if (snipResult.success && !snipResult.strokesToInsert.empty()) {
            auto delAction = std::make_unique<DeleteUndoAction>(page, false);
            for (Stroke* sOrig: snipResult.strokesToRemove) {
                std::lock_guard lock(*doc);
                auto rem = layer->removeElement(sOrig);
                if (rem.e) {
                    delAction->addElement(layer, std::move(rem.e), rem.pos);
                }
            }
            for (auto& sNew: snipResult.strokesToInsert) {
                Stroke* ptr = sNew.get();
                {
                    std::lock_guard lock(*doc);
                    layer->addElement(std::move(sNew));
                }
                newlyShapedElements.push_back(ptr);
                groupUndo->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
            }
            groupUndo->addAction(std::move(delAction));

            for (Stroke* sText: snipResult.protectedTextStrokes) {
                newlyShapedElements.push_back(sText);
            }

            // Register undo action without holding document lock
            control->getUndoRedoHandler()->addUndoAction(std::move(groupUndo));

            // Reselect elements on canvas
            InsertionOrderRef refs;
            for (const auto& elem: layer->getElements()) {
                if (!elem) continue;
                bool isSelected = false;
                for (Element* rep: newlyShapedElements) {
                    if (elem.get() == rep) {
                        isSelected = true;
                        break;
                    }
                }
                if (isSelected) {
                    Element::Index idx = layer->indexOf(elem.get());
                    if (idx != Element::InvalidIndex) {
                        refs.emplace_back(elem.get(), idx);
                    }
                }
            }

            if (!refs.empty()) {
                std::sort(refs.begin(), refs.end());
                size_t pageNo = doc->indexOf(page);
                XojPageView* targetView = control->getWindow()->getXournal()->getViewFor(pageNo);
                if (!targetView) {
                    targetView = view;
                }
                auto newSel = SelectionFactory::createFromElementsOnActiveLayer(control, page, targetView, refs);
                if (newSel) {
                    control->getWindow()->getXournal()->setSelection(newSel.release());
                }
            }

            return true;
        }
    }

    // Process multi-stroke ground symbols first
    auto multiGrounds = detectMultiStrokeGrounds(candidates);
    auto groundTpl = customMgr ? customMgr->getTemplateById("ground") : nullptr;
    if (groundTpl) {
        for (const auto& g: multiGrounds) {
            const Stroke* styleSrc = g.stem ? g.stem : (g.bars.empty() ? nullptr : g.bars.front());
            auto compStrokes = CircuitSnapper::snapCircuitComposite(groundTpl, g.topPt, g.bottomPt, styleSrc, true);
            if (compStrokes.empty()) continue;

            auto delAction = std::make_unique<DeleteUndoAction>(page, false);

            std::vector<Stroke*> toRemove = g.bars;
            if (g.stem) {
                toRemove.push_back(g.stem);
            }

            for (Stroke* sOrig: toRemove) {
                consumedStrokes.insert(sOrig);
                std::lock_guard lock(*doc);
                auto rem = layer->removeElement(sOrig);
                if (rem.e) {
                    delAction->addElement(layer, std::move(rem.e), rem.pos);
                }
            }

            for (auto& sNew: compStrokes) {
                Stroke* ptr = sNew.get();
                {
                    std::lock_guard lock(*doc);
                    layer->addElement(std::move(sNew));
                }
                newlyShapedElements.push_back(ptr);
                groupUndo->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
            }

            groupUndo->addAction(std::move(delAction));
        }
    }

    // Pass 1: Handwriting & Annotation Protection (Block & Proximity Clustering)
    // Segregate text words ('34K', '22K', '50', 'is', 'RE 1,5', 'RC 2K')
    // so they are NEVER modified, turned into shapes, or magnetically pulled into nodes.
    std::set<Stroke*> protectedHandwriting = CircuitDecomposer::clusterTextBlocks(candidates);
    for (Stroke* s: candidates) {
        if (!s || consumedStrokes.count(s)) continue;
        if (CircuitFeatureClassifier::isHandwritingOrAnnotation(s)) {
            protectedHandwriting.insert(s);
        }
    }

    // Pass 1.5: Multi-Stroke BJT Transistor Assembly
    auto bjtMatches = CircuitFeatureClassifier::detectBjtTransistors(candidates);
    for (const auto& match: bjtMatches) {
        if (consumedStrokes.count(match.baseBar) || consumedStrokes.count(match.emitter) || consumedStrokes.count(match.collector)) {
            continue;
        }

        const Stroke* styleSrc = match.baseBar;
        auto bjtStrokes = CircuitSnapper::snapBjtTransistor(match, styleSrc);
        if (!bjtStrokes.empty()) {
            auto delAction = std::make_unique<DeleteUndoAction>(page, false);
            std::vector<Stroke*> toRemove = {match.baseBar, match.emitter, match.collector};

            for (Stroke* sOrig: toRemove) {
                consumedStrokes.insert(sOrig);
                std::lock_guard lock(*doc);
                auto rem = layer->removeElement(sOrig);
                if (rem.e) {
                    delAction->addElement(layer, std::move(rem.e), rem.pos);
                }
            }

            for (auto& sNew: bjtStrokes) {
                Stroke* ptr = sNew.get();
                {
                    std::lock_guard lock(*doc);
                    layer->addElement(std::move(sNew));
                }
                newlyShapedElements.push_back(ptr);
                groupUndo->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
            }

            groupUndo->addAction(std::move(delAction));
        }
    }

    // Anchor Nodes for components, BJT, and ground terminals
    struct AnchorNode {
        Point pt;
        Point direction; // Optional preferred orientation
    };
    std::vector<AnchorNode> anchorNodes;

    // Record anchors from BJT transistors
    for (const auto& match: bjtMatches) {
        anchorNodes.push_back({match.basePin, Point()});
        anchorNodes.push_back({match.collectorPin, Point()});
        anchorNodes.push_back({match.emitterPin, Point()});
    }

    // Record anchors from multi-stroke grounds
    for (const auto& g: multiGrounds) {
        anchorNodes.push_back({g.topPt, Point(0.0, -1.0)});
    }

    ShapeRecognizer standardRecognizer;

    for (Stroke* stroke: candidates) {
        if (!stroke || consumedStrokes.count(stroke) || stroke->getPointCount() < 2 ||
            protectedHandwriting.count(stroke)) {
            continue;
        }

        // Check if this is a compound stroke (lead + resistor body + lead)
        auto decomposedParts = CircuitDecomposer::decomposeCompoundStroke(stroke);
        if (!decomposedParts.empty() && customMgr) {
            auto rTpl = customMgr->getTemplateById("resistor_ieee");
            if (rTpl) {
                auto delAction = std::make_unique<DeleteUndoAction>(page, false);
                Element::Index pos = Element::InvalidIndex;
                {
                    std::lock_guard lock(*doc);
                    auto rem = layer->removeElement(stroke);
                    if (rem.e) {
                        pos = rem.pos;
                        delAction->addElement(layer, std::move(rem.e), pos);
                    }
                }

                for (auto& part: decomposedParts) {
                    if (part.isResistorBody) {
                        auto compStrokes = customMgr->recognizeComposite(part.stroke.get(), nullptr, 0.50);
                        if (!compStrokes.empty()) {
                            for (auto& sNew: compStrokes) {
                                Stroke* ptr = sNew.get();
                                {
                                    std::lock_guard lock(*doc);
                                    layer->addElement(std::move(sNew));
                                }
                                newlyShapedElements.push_back(ptr);
                                groupUndo->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
                            }
                            continue;
                        }
                    } else if (part.isWireLead) {
                        auto wire = snapStraightWire(part.stroke.get());
                        if (wire) {
                            Stroke* ptr = wire.get();
                            {
                                std::lock_guard lock(*doc);
                                layer->addElement(std::move(wire));
                            }
                            newlyShapedElements.push_back(ptr);
                            groupUndo->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
                            continue;
                        }
                    }

                    // Fallback: keep decomposed stroke as is
                    Stroke* ptr = part.stroke.get();
                    {
                        std::lock_guard lock(*doc);
                        layer->addElement(std::move(part.stroke));
                    }
                    newlyShapedElements.push_back(ptr);
                    groupUndo->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
                }

                groupUndo->addAction(std::move(delAction));
                consumedStrokes.insert(stroke);
                continue;
            }
        }

        std::unique_ptr<Stroke> replacement = nullptr;

        // Try 1: Custom SVG circuit symbols (resistor, inductor, capacitor, diode, etc.)
        if (customMgr) {
            auto compStrokes = customMgr->recognizeComposite(stroke, nullptr, 0.60);
            if (compStrokes.size() > 1) {
                // Multi-stroke composite (e.g. Capacitor with air gap)
                auto delAction = std::make_unique<DeleteUndoAction>(page, false);
                Element::Index pos = Element::InvalidIndex;
                {
                    std::lock_guard lock(*doc);
                    auto rem = layer->removeElement(stroke);
                    if (rem.e) {
                        pos = rem.pos;
                        delAction->addElement(layer, std::move(rem.e), pos);
                    }
                }

                for (auto& sNew: compStrokes) {
                    Stroke* ptr = sNew.get();
                    if (ptr->getPointCount() >= 2) {
                        anchorNodes.push_back({ptr->getPoint(0), Point()});
                        anchorNodes.push_back({ptr->getPoint(ptr->getPointCount() - 1), Point()});
                    }
                    {
                        std::lock_guard lock(*doc);
                        layer->addElement(std::move(sNew));
                    }
                    newlyShapedElements.push_back(ptr);
                    groupUndo->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
                }

                groupUndo->addAction(std::move(delAction));
                consumedStrokes.insert(stroke);
                continue;
            } else if (compStrokes.size() == 1) {
                replacement = std::move(compStrokes[0]);
            }
        }

        // Try 2.5: Arrow Recognition
        if (!replacement) {
            Point shaftStart, tip;
            if (CircuitFeatureClassifier::detectArrow(stroke, shaftStart, tip)) {
                // Generate an Arrow shape
                double dx = tip.x - shaftStart.x;
                double dy = tip.y - shaftStart.y;
                double len = std::hypot(dx, dy);
                if (len >= 16.0) {
                    // Ortho snap if roughly horizontal or vertical
                    double angle = std::atan2(dy, dx);
                    constexpr double SNAP_TOL = 25.0 * (M_PI / 180.0);
                    if (std::abs(angle) < SNAP_TOL || std::abs(std::abs(angle) - M_PI) < SNAP_TOL) {
                        tip.y = shaftStart.y;
                        len = std::abs(tip.x - shaftStart.x);
                        angle = (tip.x >= shaftStart.x) ? 0.0 : M_PI;
                    } else if (std::abs(std::abs(angle) - M_PI * 0.5) < SNAP_TOL) {
                        tip.x = shaftStart.x;
                        len = std::abs(tip.y - shaftStart.y);
                        angle = (tip.y >= shaftStart.y) ? (M_PI * 0.5) : (-M_PI * 0.5);
                    }

                    double headLen = std::clamp(len * 0.35, 6.0, 16.0);
                    double headAngle = M_PI / 6.0; // 30 deg

                    auto arrowStroke = std::make_unique<Stroke>(*stroke);
                    std::vector<Point> aPts;
                    aPts.reserve(6);
                    aPts.push_back(shaftStart);
                    aPts.push_back(tip);
                    // Wing 1
                    aPts.emplace_back(tip.x - headLen * std::cos(angle - headAngle),
                                      tip.y - headLen * std::sin(angle - headAngle));
                    aPts.push_back(tip);
                    // Wing 2
                    aPts.emplace_back(tip.x - headLen * std::cos(angle + headAngle),
                                      tip.y - headLen * std::sin(angle + headAngle));
                    arrowStroke->setPointVector(std::move(aPts));
                    replacement = std::move(arrowStroke);
                }
            }
        }

        // Try 3: Standard geometric shapes (circles, ellipses, rects, triangles)
        if (!replacement) {
            auto recogElement = standardRecognizer.recognizePatterns(stroke, 10.0);
            if (recogElement && recogElement->getType() == ELEMENT_STROKE) {
                auto* sRecog = static_cast<Stroke*>(recogElement.get());
                // Protect elongated lines/wires from being turned into degenerate triangles or polygons
                auto sBox = stroke->getBoundingBox();
                double aspect = (sBox.width > 0 && sBox.height > 0)
                                    ? (std::max(sBox.width, sBox.height) / std::min(sBox.width, sBox.height))
                                    : 100.0;
                if (aspect <= 4.0 || sRecog->getPointCount() > 16) {
                    replacement.reset(static_cast<Stroke*>(recogElement.release()));
                }
            }
        }

        // Try 3: AC Source internal sine wave inside a circle
        if (!replacement) {
            for (Element* otherElem: newlyShapedElements) {
                if (otherElem && otherElem->getType() == ELEMENT_STROKE) {
                    auto* otherStroke = static_cast<Stroke*>(otherElem);
                    auto box = otherStroke->getBoundingBox();
                    // Check if otherStroke is roughly circular/square (e.g. AC circle)
                    if (std::abs(box.width - box.height) <= std::max(box.width, box.height) * 0.25 &&
                        std::max(box.width, box.height) >= 20.0) {
                        Point center(box.x + box.width * 0.5, box.y + box.height * 0.5);
                        double radius = (box.width + box.height) * 0.25;
                        replacement = snapSineWave(stroke, center, radius);
                        if (replacement) break;
                    }
                }
            }
        }

        // Try 4: L-shaped corner wire
        if (!replacement) {
            replacement = snapCornerWire(stroke);
        }

        // Try 5: Straight connecting wire
        if (!replacement) {
            replacement = snapStraightWire(stroke);
        }

        if (replacement) {
            if (replacement->getPointCount() >= 2) {
                anchorNodes.push_back({replacement->getPoint(0), Point()});
                anchorNodes.push_back({replacement->getPoint(replacement->getPointCount() - 1), Point()});
            }

            // Replace stroke in layer, locking doc only for the brief layer mutation
            Stroke* repPtr = replacement.get();
            ElementPtr ownedOriginal;
            Element::Index pos = Element::InvalidIndex;

            {
                std::lock_guard lock(*doc);
                auto removedPos = layer->removeElement(stroke);
                ownedOriginal = std::move(removedPos.e);
                pos = removedPos.pos;
                if (ownedOriginal) {
                    layer->insertElement(std::move(replacement), pos);
                }
            }

            if (ownedOriginal) {
                newlyShapedElements.push_back(repPtr);

                groupUndo->addAction(std::make_unique<RecognizerUndoAction>(
                        page, layer, std::move(ownedOriginal), repPtr));
            }
        }
    }

    if (newlyShapedElements.empty()) {
        return false;
    }

    // 2. Manhattan T-Junction Snapping:
    // Ensure vertical wires meeting horizontal rails (and vice versa) snap into exact 90-degree right angles
    applyTJunctionSnapping(newlyShapedElements);

    // 3. Terminal & Node Snapping:
    // Connect wire endpoints and component terminals that lie within 16 px of each other
    constexpr double NODE_PROXIMITY_SQ = 16.0 * 16.0;

    // Collect all endpoints of newly shaped strokes ONLY (NEVER touch handwriting or unshaped strokes)
    struct EndpointRef {
        Stroke* stroke;
        bool isStart;
        Point pt;
    };
    std::vector<EndpointRef> endpoints;

    for (Element* elem: newlyShapedElements) {
        auto* s = static_cast<Stroke*>(elem);
        // Never pull endpoints of handwriting
        if (protectedHandwriting.count(s)) continue;
        // Never pull endpoints of complex components (resistors, inductors, AC waves, arrows, grounds)
        if (s->getPointCount() > 2) continue;

        const auto& pts = s->getPointVector();
        if (pts.size() >= 2) {
            endpoints.push_back({s, true, pts.front()});
            endpoints.push_back({s, false, pts.back()});
        }
    }

    // Cluster close endpoints into junction nodes
    for (size_t i = 0; i < endpoints.size(); ++i) {
        for (size_t j = i + 1; j < endpoints.size(); ++j) {
            if (endpoints[i].stroke == endpoints[j].stroke) {
                continue;
            }
            const double dx = endpoints[i].pt.x - endpoints[j].pt.x;
            const double dy = endpoints[i].pt.y - endpoints[j].pt.y;
            double distSq = dx * dx + dy * dy;

            if (distSq <= NODE_PROXIMITY_SQ) {
                Point junction((endpoints[i].pt.x + endpoints[j].pt.x) * 0.5,
                               (endpoints[i].pt.y + endpoints[j].pt.y) * 0.5);
                endpoints[i].pt = junction;
                endpoints[j].pt = junction;
            }
        }
    }

    // Apply snapped endpoints back to the strokes (adjusting only endpoints of simple 2-point wire segments)
    for (const auto& ep: endpoints) {
        if (ep.stroke->getPointCount() != 2) {
            // Never distort interior points of complex shaped components (resistors, inductors, AC waves)
            continue;
        }
        auto pts = ep.stroke->getPointVector();
        if (pts.size() < 2) continue;
        if (ep.isStart) {
            double shift = ep.pt.lineLengthTo(pts.front());
            if (shift <= 12.0) {
                pts.front() = ep.pt;
            }
        } else {
            double shift = ep.pt.lineLengthTo(pts.back());
            if (shift <= 12.0) {
                pts.back() = ep.pt;
            }
        }
        ep.stroke->setPointVector(std::move(pts));
    }

    // 4. Register Undo action (NO doc lock held, so updateWindowTitle can acquire shared lock cleanly)
    control->getUndoRedoHandler()->addUndoAction(std::move(groupUndo));

    // 5. Reselect elements that were in the selection (both shaped components and untouched elements)
    InsertionOrderRef refs;
    for (const auto& elem: layer->getElements()) {
        if (!elem) continue;
        bool isSelected = false;
        for (Element* rep: newlyShapedElements) {
            if (elem.get() == rep) {
                isSelected = true;
                break;
            }
        }
        if (!isSelected) {
            for (const Element* orig: selectedElementPtrs) {
                if (elem.get() == orig) {
                    isSelected = true;
                    break;
                }
            }
        }
        if (isSelected) {
            Element::Index idx = layer->indexOf(elem.get());
            if (idx != Element::InvalidIndex) {
                refs.emplace_back(elem.get(), idx);
            }
        }
    }

    if (!refs.empty()) {
        std::sort(refs.begin(), refs.end());
        size_t pageNo = doc->indexOf(page);
        XojPageView* targetView = control->getWindow()->getXournal()->getViewFor(pageNo);
        if (!targetView) {
            targetView = view;
        }
        auto newSel = SelectionFactory::createFromElementsOnActiveLayer(control, page, targetView, refs);
        if (newSel) {
            control->getWindow()->getXournal()->setSelection(newSel.release());
        }
    }

    Range totalRange(0.0, 0.0, page->getWidth(), page->getHeight());
    page->fireRangeChanged(totalRange);
    view->getXournal()->repaintSelection(true);

    return true;
}

}  // namespace xoj::circuit
