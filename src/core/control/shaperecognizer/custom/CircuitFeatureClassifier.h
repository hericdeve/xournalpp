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
};

}  // namespace xoj::circuit
