#pragma once

#include <memory>
#include <set>
#include <vector>

#include "model/Point.h"
#include "model/Stroke.h"

namespace xoj::circuit {

/**
 * Result of decomposing a compound stroke (e.g. wire lead + resistor body + wire lead)
 */
struct DecomposedStrokePart {
    std::unique_ptr<Stroke> stroke;
    bool isResistorBody = false;
    bool isWireLead = false;
};

class CircuitDecomposer {
public:
    /**
     * Groups nearby handwriting strokes into protected text clusters.
     * Strokes within 20px of each other that exhibit handwriting characteristics
     * (small dimensions, compact aspect ratio, or high curvature) are aggregated
     * into word clusters ("R1", "22K", "RE", "1,5", "RC", "2K", "34K").
     *
     * @param candidates All candidate strokes in the selection.
     * @return Set of strokes that belong to protected text blocks.
     */
    static auto clusterTextBlocks(const std::vector<Stroke*>& candidates) -> std::set<Stroke*>;

    /**
     * Inspects a stroke to determine if it is a compound stroke (e.g. straight lead
     * transitioning into an oscillating resistor zig-zag, followed by another lead).
     * If compound, splits the stroke at transition vertices into sub-strokes.
     *
     * @param stroke The stroke to decompose.
     * @return Vector of decomposed sub-strokes. If not compound, returns empty vector.
     */
    static auto decomposeCompoundStroke(const Stroke* stroke) -> std::vector<DecomposedStrokePart>;

    /**
     * Calculates the average interior angle across local turning vertices of a stroke.
     * Sharp V-teeth (resistors) have acute angles (<= 85 deg).
     * Rounded U-coils (inductors) have obtuse angles (>= 115 deg).
     */
    static auto computeAverageVertexAngle(const Stroke* stroke) -> double;
};

}  // namespace xoj::circuit
