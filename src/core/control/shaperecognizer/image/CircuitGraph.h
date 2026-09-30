/*
 * Xournal++
 * Circuit Diagram Shape Recognition Engine
 *
 * Topological Circuit Graph Extracted from Morphological Skeleton
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "BinaryGrid.h"
#include "model/Point.h"

namespace xoj::circuit {

enum class BranchType {
    Wire,
    Resistor,
    Inductor,
    Unknown
};

struct GraphNode {
    int id = -1;
    Point pos;
    int degree = 0;              // 1 = terminal / endpoint, >= 3 = junction
    std::vector<int> incidentBranches;  // indices of branches connected to this node
    std::vector<std::pair<int, int>> rawPixels;  // constituent pixels
};

struct GraphBranch {
    int id = -1;
    int nodeA = -1;
    int nodeB = -1;
    std::vector<std::pair<int, int>> pixelPath;
    double chordLength = 0.0;
    double arcLength = 0.0;
    double sinuosity = 1.0;
    double avgVertexAngleDeg = 180.0;
    int sharpCornerCount = 0;
    BranchType type = BranchType::Wire;
};

struct GraphBjtTransistor {
    int baseNode = -1;
    int collectorNode = -1;
    int emitterNode = -1;
    Point centerPos;
    bool isNpn = true;
    std::vector<int> consumedBranches;
};

struct GraphGround {
    int terminalNode = -1;
    Point terminalPos;
    double width = 24.0;
    std::vector<int> consumedBranches;
};

class CircuitGraph {
public:
    CircuitGraph() = default;
    ~CircuitGraph() = default;

    /**
     * @brief Build topological graph from a thinned 1-pixel skeleton grid.
     */
    static auto extractFromSkeleton(const BinaryGrid& grid) -> CircuitGraph;

    [[nodiscard]] auto getNodes() const -> const std::vector<GraphNode>& { return nodes; }
    [[nodiscard]] auto getBranches() const -> const std::vector<GraphBranch>& { return branches; }
    [[nodiscard]] auto getTransistors() const -> const std::vector<GraphBjtTransistor>& { return transistors; }
    [[nodiscard]] auto getGrounds() const -> const std::vector<GraphGround>& { return grounds; }

private:
    std::vector<GraphNode> nodes;
    std::vector<GraphBranch> branches;
    std::vector<GraphBjtTransistor> transistors;
    std::vector<GraphGround> grounds;

    void analyzeBranches();
    void pruneSpursAndMerge();
    void detectBjtTransistors();
    void detectGrounds();
};

}  // namespace xoj::circuit
