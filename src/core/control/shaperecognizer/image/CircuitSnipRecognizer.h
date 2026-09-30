/*
 * Xournal++
 * Circuit Diagram Shape Recognition Engine
 *
 * Image-Based Snip / Topological Circuit Recognizer
 */

#pragma once

#include <memory>
#include <vector>

#include "BinaryGrid.h"
#include "CircuitGraph.h"
#include "ZhangSuenThinner.h"
#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "model/Point.h"
#include "model/Stroke.h"

namespace xoj::circuit {

struct CircuitSnipResult {
    bool success = false;
    std::vector<Stroke*> strokesToRemove;
    std::vector<std::unique_ptr<Stroke>> strokesToInsert;
    std::vector<Stroke*> protectedTextStrokes;
};

class CircuitSnipRecognizer {
public:
    CircuitSnipRecognizer() = default;
    ~CircuitSnipRecognizer() = default;

    /**
     * @brief Process a selection of strokes using the 2D Image Snip & Topological Skeleton engine.
     * @param selectedStrokes The active strokes selected by the user.
     * @param shapeManager Custom shape template manager for SVG circuit symbols.
     * @return Snip result containing strokes to replace and new crisp vector strokes.
     */
    static auto processSnip(const std::vector<Stroke*>& selectedStrokes,
                            CustomShapeManager* shapeManager) -> CircuitSnipResult;

    /**
     * @brief Rasterize strokes into a BinaryGrid with padding.
     */
    static auto rasterizeStrokes(const std::vector<Stroke*>& strokes,
                                 double minX, double minY, int width, int height,
                                 double padding = 20.0) -> BinaryGrid;
};

}  // namespace xoj::circuit
