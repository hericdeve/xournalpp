/*
 * Xournal++
 * Circuit Diagram Shape Recognition Engine
 *
 * Zhang-Suen Morphological Skeletonization Algorithm
 */

#pragma once

#include "BinaryGrid.h"

namespace xoj::circuit {

class ZhangSuenThinner {
public:
    ZhangSuenThinner() = default;
    ~ZhangSuenThinner() = default;

    /**
     * @brief Apply Zhang-Suen thinning in-place to the binary grid.
     * @param grid The binary grid to thin to 1-pixel skeleton.
     * @param maxIterations Safety cutoff for iterations (default 50).
     * @return Number of iterations performed until convergence.
     */
    static auto thin(BinaryGrid& grid, int maxIterations = 50) -> int;
};

}  // namespace xoj::circuit
