/*
 * Xournal++
 *
 * Circuit Structural Feature Classifier
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>
#include <memory>
#include "model/Point.h"
#include "model/Stroke.h"

namespace xoj::circuit {

enum class CircuitFeatureClass {
    Unknown,
    ResistorIeee,
    Inductor,
    Capacitor,
    SineWave,
    Arrow,
    Ground,
    TransistorBjt,
    StraightWire,
    CornerWire
};

struct StrokeExtremum {
    size_t index = 0;
    double u = 0.0;    // coordinate along baseline chord [0, 1]
    double v = 0.0;    // perpendicular deviation
    bool isPositive = true; // true = peak (> 0), false = valley (< 0)
};

struct StrokeStructuralFeatures {
    double chordLength = 0.0;
    double arcLength = 0.0;
    double sinuosity = 1.0;
    double maxPerpPositive = 0.0;
    double maxPerpNegative = 0.0;
    double chordAngle = 0.0;
    Point chordStart{0.0, 0.0};
    Point chordEnd{0.0, 0.0};

    // Body subsegment details (where active oscillations occur)
    double bodyStartU = 0.0;
    double bodyEndU = 1.0;
    double bodySinuosity = 1.0;
    bool hasOscillatingBody = false;

    std::vector<StrokeExtremum> extrema;
    size_t positivePeakCount = 0;
    size_t negativeValleyCount = 0;
    size_t alternatingExtremaCount = 0;
    bool isUnipolar = false;
};

class CircuitFeatureClassifier {
public:
    /**
     * Extracts structural features (baseline chord, perpendicular extrema, oscillation)
     * from a candidate stroke.
     */
    static auto extractFeatures(const Stroke* stroke) -> StrokeStructuralFeatures;

    /**
     * Classifies stroke based on structural and topological invariants.
     */
    static auto classify(const Stroke* stroke) -> CircuitFeatureClass;

    /**
     * Determines whether a stroke represents handwriting text, digits, or annotations
     * that should be protected from shape conversion and wire snapping.
     */
    static auto isHandwritingOrAnnotation(const Stroke* stroke) -> bool;

    /**
     * Detects if a stroke is an arrow (shaft + head drawn in single stroke).
     * Returns true and sets outShaftStart and outTip if detected.
     */
    static auto detectArrow(const Stroke* stroke, Point& outShaftStart, Point& outTip) -> bool;

    /**
     * Detects if a stroke is a single-stroke ground symbol (stem + bottom bar/cross).
     * Returns true and sets outTopPt and outBottomPt if detected.
     */
    static auto detectSingleStrokeGround(const Stroke* stroke, Point& outTopPt, Point& outBottomPt) -> bool;

    /**
     * Detects BJT transistors (base bar + slanted emitter/collector leads, optional arrowhead)
     * across a set of candidate strokes.
     */
    static auto detectBjtTransistors(const std::vector<Stroke*>& candidates) -> std::vector<struct BjtTransistorMatch>;
};

struct BjtTransistorMatch {
    Stroke* baseBar = nullptr;
    Stroke* emitter = nullptr;
    Stroke* collector = nullptr;
    bool isPnp = false;
    Point basePin;
    Point collectorPin;
    Point emitterPin;
    Point baseCenter;
    bool isVertical = true;
};

}  // namespace xoj::circuit
