/*
 * Xournal++
 *
 * Circuit Component Geometric Snapper & Lead Stretcher
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>

#include "CircuitTemplate.h"
#include "control/tools/SnapToGridInputHandler.h"
#include "model/Point.h"
#include "model/Stroke.h"

namespace xoj::circuit {

class CircuitSnapper {
public:
    static auto snapCircuit(const CircuitTemplate* tpl, const Point& startPt, const Point& endPt,
                            const Stroke* styleSource, bool orthoSnap = true,
                            SnapToGridInputHandler* snappingHandler = nullptr) -> std::unique_ptr<Stroke>;

    static auto snapAngle(double angle, double cardinalTolerance = 0.28, double diagonalTolerance = 0.14) -> double;
};

}  // namespace xoj::circuit
