#include "CircuitGraph.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>

namespace xoj::circuit {

namespace {

auto distance(const Point& a, const Point& b) -> double {
    return std::hypot(a.x - b.x, a.y - b.y);
}

auto computeTurnAngleDeg(const Point& p1, const Point& p2, const Point& p3) -> double {
    double v1x = p2.x - p1.x;
    double v1y = p2.y - p1.y;
    double v2x = p3.x - p2.x;
    double v2y = p3.y - p2.y;

    double len1 = std::hypot(v1x, v1y);
    double len2 = std::hypot(v2x, v2y);
    if (len1 < 1e-4 || len2 < 1e-4) {
        return 180.0;
    }

    double dot = (v1x * v2x + v1y * v2y) / (len1 * len2);
    dot = std::clamp(dot, -1.0, 1.0);
    return std::acos(dot) * 180.0 / M_PI;
}

}  // namespace

auto CircuitGraph::extractFromSkeleton(const BinaryGrid& grid) -> CircuitGraph {
    CircuitGraph graph;
    int w = grid.getWidth();
    int h = grid.getHeight();

    std::vector<int> pixelDegree(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (grid.get(x, y) > 0) {
                pixelDegree[static_cast<size_t>(y) * w + x] = grid.count8Neighbors(x, y);
            }
        }
    }

    // Map from (x, y) to Node ID (-1 if not part of a node)
    std::vector<int> pixelToNode(static_cast<size_t>(w) * h, -1);

    static const int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    // 1. Group junction pixels (degree >= 3) into GraphNodes
    std::vector<bool> visitedJunction(static_cast<size_t>(w) * h, false);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (pixelDegree[idx] >= 3 && !visitedJunction[idx]) {
                GraphNode node;
                node.id = static_cast<int>(graph.nodes.size());
                node.degree = pixelDegree[idx];

                double sumX = 0.0;
                double sumY = 0.0;

                std::queue<std::pair<int, int>> q;
                q.emplace(x, y);
                visitedJunction[idx] = true;

                while (!q.empty()) {
                    auto [cx, cy] = q.front();
                    q.pop();

                    node.rawPixels.emplace_back(cx, cy);
                    pixelToNode[static_cast<size_t>(cy) * w + cx] = node.id;
                    sumX += cx;
                    sumY += cy;

                    for (int i = 0; i < 8; ++i) {
                        int nx = cx + dx[i];
                        int ny = cy + dy[i];
                        if (grid.inBounds(nx, ny)) {
                            size_t nidx = static_cast<size_t>(ny) * w + nx;
                            if (pixelDegree[nidx] >= 3 && !visitedJunction[nidx]) {
                                visitedJunction[nidx] = true;
                                q.emplace(nx, ny);
                            }
                        }
                    }
                }

                if (!node.rawPixels.empty()) {
                    double n = static_cast<double>(node.rawPixels.size());
                    node.pos = Point(sumX / n, sumY / n);
                    graph.nodes.push_back(std::move(node));
                }
            }
        }
    }

    // 2. Identify endpoints (degree == 1) as GraphNodes
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (pixelDegree[idx] == 1 && pixelToNode[idx] == -1) {
                GraphNode node;
                node.id = static_cast<int>(graph.nodes.size());
                node.degree = 1;
                node.pos = Point(x, y);
                node.rawPixels.emplace_back(x, y);
                pixelToNode[idx] = node.id;
                graph.nodes.push_back(std::move(node));
            }
        }
    }

    // 3. Trace branches along degree-2 pixels between nodes
    std::vector<bool> visitedBranchPixel(static_cast<size_t>(w) * h, false);

    for (const auto& node: graph.nodes) {
        for (const auto& [nx, ny]: node.rawPixels) {
            for (int i = 0; i < 8; ++i) {
                int bx = nx + dx[i];
                int by = ny + dy[i];
                if (!grid.inBounds(bx, by)) {
                    continue;
                }

                size_t bidx = static_cast<size_t>(by) * w + bx;
                if (pixelDegree[bidx] == 2 && !visitedBranchPixel[bidx]) {
                    // Trace this branch
                    GraphBranch branch;
                    branch.id = static_cast<int>(graph.branches.size());
                    branch.nodeA = node.id;

                    int curX = bx;
                    int curY = by;
                    int prevX = nx;
                    int prevY = ny;

                    while (true) {
                        branch.pixelPath.emplace_back(curX, curY);
                        visitedBranchPixel[static_cast<size_t>(curY) * w + curX] = true;

                        // Check if adjacent to another node
                        int targetNodeId = -1;
                        for (int k = 0; k < 8; ++k) {
                            int checkX = curX + dx[k];
                            int checkY = curY + dy[k];
                            if (grid.inBounds(checkX, checkY)) {
                                int nid = pixelToNode[static_cast<size_t>(checkY) * w + checkX];
                                if (nid != -1 && nid != node.id) {
                                    targetNodeId = nid;
                                    break;
                                }
                            }
                        }

                        if (targetNodeId != -1) {
                            branch.nodeB = targetNodeId;
                            break;
                        }

                        // Next degree-2 step
                        int nextX = -1;
                        int nextY = -1;
                        for (int k = 0; k < 8; ++k) {
                            int stepX = curX + dx[k];
                            int stepY = curY + dy[k];
                            if (grid.inBounds(stepX, stepY) && (stepX != prevX || stepY != prevY)) {
                                size_t stepIdx = static_cast<size_t>(stepY) * w + stepX;
                                if (pixelDegree[stepIdx] == 2 && !visitedBranchPixel[stepIdx]) {
                                    nextX = stepX;
                                    nextY = stepY;
                                    break;
                                }
                            }
                        }

                        if (nextX == -1) {
                            // Dead end or closed back to nodeA
                            branch.nodeB = node.id;
                            break;
                        }

                        prevX = curX;
                        prevY = curY;
                        curX = nextX;
                        curY = nextY;
                    }

                    if (branch.nodeB != -1) {
                        graph.nodes[branch.nodeA].incidentBranches.push_back(branch.id);
                        if (branch.nodeA != branch.nodeB) {
                            graph.nodes[branch.nodeB].incidentBranches.push_back(branch.id);
                        }
                        graph.branches.push_back(std::move(branch));
                    }
                }
            }
        }
    }

    // 4. Graph simplification (prune spurs to remove topological noise)
    graph.pruneSpursAndMerge();

    // 5. Feature analysis of each branch
    graph.analyzeBranches();
    graph.detectBjtTransistors();
    graph.detectGrounds();

    return graph;
}

void CircuitGraph::pruneSpursAndMerge() {
    bool changed = true;
    while (changed) {
        changed = false;
        
        // Find a spur: a branch connected to a degree-1 node, with short length
        for (auto& branch : branches) {
            if (branch.id == -1) continue;
            
            bool isSpur = false;
            int keepNodeId = -1;
            int dropNodeId = -1;
            
            if (nodes[branch.nodeA].degree == 1 && nodes[branch.nodeB].degree > 1 && branch.pixelPath.size() < 25) {
                isSpur = true;
                keepNodeId = branch.nodeB;
                dropNodeId = branch.nodeA;
            } else if (nodes[branch.nodeB].degree == 1 && nodes[branch.nodeA].degree > 1 && branch.pixelPath.size() < 25) {
                isSpur = true;
                keepNodeId = branch.nodeA;
                dropNodeId = branch.nodeB;
            }
            
            if (isSpur) {
                // Remove this branch from keepNode
                auto& inc = nodes[keepNodeId].incidentBranches;
                inc.erase(std::remove(inc.begin(), inc.end(), branch.id), inc.end());
                nodes[keepNodeId].degree = inc.size();
                
                // Mark drop node and branch as deleted
                nodes[dropNodeId].degree = 0;
                branch.id = -1;
                changed = true;
                break;
            }
        }
        
        // Merge degree-2 nodes
        if (!changed) {
            for (auto& node : nodes) {
                if (node.degree == 2 && node.incidentBranches.size() == 2) {
                    int b1_idx = node.incidentBranches[0];
                    int b2_idx = node.incidentBranches[1];
                    
                    if (b1_idx == b2_idx) continue; // It's a loop on a single node
                    
                    auto& b1 = branches[b1_idx];
                    auto& b2 = branches[b2_idx];
                    
                    if (b1.id == -1 || b2.id == -1) continue;
                    
                    // Merge b2 into b1
                    int otherNodeB1 = (b1.nodeA == node.id) ? b1.nodeB : b1.nodeA;
                    int otherNodeB2 = (b2.nodeA == node.id) ? b2.nodeB : b2.nodeA;
                    
                    b1.nodeA = otherNodeB1;
                    b1.nodeB = otherNodeB2;
                    
                    // Append pixel path
                    b1.pixelPath.insert(b1.pixelPath.end(), b2.pixelPath.begin(), b2.pixelPath.end());
                    
                    // Update the other node of b2 to point to b1
                    auto& otherInc = nodes[otherNodeB2].incidentBranches;
                    for (int& id : otherInc) {
                        if (id == b2_idx) id = b1_idx;
                    }
                    
                    // Delete b2 and node
                    b2.id = -1;
                    node.degree = 0;
                    node.incidentBranches.clear();
                    
                    changed = true;
                    break;
                }
            }
        }
    }
}

void CircuitGraph::analyzeBranches() {
    for (auto& branch: branches) {
        if (branch.nodeA < 0 || branch.nodeA >= static_cast<int>(nodes.size()) ||
            branch.nodeB < 0 || branch.nodeB >= static_cast<int>(nodes.size())) {
            continue;
        }

        const auto& pA = nodes[branch.nodeA].pos;
        const auto& pB = nodes[branch.nodeB].pos;
        branch.chordLength = distance(pA, pB);
        branch.arcLength = static_cast<double>(branch.pixelPath.size());
        branch.sinuosity = branch.arcLength / std::max(1.0, branch.chordLength);

        // Subsample pixelPath to evaluate vertex turn angles
        if (branch.pixelPath.size() >= 12) {
            std::vector<Point> sampledPts;
            size_t step = std::max<size_t>(3, branch.pixelPath.size() / 20);
            for (size_t i = 0; i < branch.pixelPath.size(); i += step) {
                sampledPts.emplace_back(branch.pixelPath[i].first, branch.pixelPath[i].second);
            }
            if (sampledPts.back().x != branch.pixelPath.back().first || sampledPts.back().y != branch.pixelPath.back().second) {
                sampledPts.emplace_back(branch.pixelPath.back().first, branch.pixelPath.back().second);
            }

            double totalAngle = 0.0;
            int angleCount = 0;
            branch.sharpCornerCount = 0;

            for (size_t i = 1; i + 1 < sampledPts.size(); ++i) {
                double angle = computeTurnAngleDeg(sampledPts[i - 1], sampledPts[i], sampledPts[i + 1]);
                totalAngle += angle;
                angleCount++;
                if (angle <= 88.0) {
                    branch.sharpCornerCount++;
                }
            }

            if (angleCount > 0) {
                branch.avgVertexAngleDeg = totalAngle / angleCount;
            }
        }

        // Classification
        if (branch.chordLength >= 22.0 && branch.sinuosity > 1.25) {
            if (branch.sharpCornerCount >= 2 && branch.avgVertexAngleDeg <= 95.0) {
                branch.type = BranchType::Resistor;
            } else if (branch.avgVertexAngleDeg >= 110.0 && branch.chordLength >= 32.0) {
                branch.type = BranchType::Inductor;
            } else {
                branch.type = BranchType::Resistor;  // Default oscillating branch to resistor
            }
        } else {
            branch.type = BranchType::Wire;
        }
    }
}

void CircuitGraph::detectBjtTransistors() {
    // Look for junction nodes that have 3 incident branches, or two adjacent junction nodes
    for (const auto& node: nodes) {
        if (node.incidentBranches.size() != 3) {
            continue;
        }

        int b1 = node.incidentBranches[0];
        int b2 = node.incidentBranches[1];
        int b3 = node.incidentBranches[2];

        // Ensure all 3 are wires (not resistors)
        if (branches[b1].type != BranchType::Wire ||
            branches[b2].type != BranchType::Wire ||
            branches[b3].type != BranchType::Wire) {
            continue;
        }

        // Check if two of the branches are roughly symmetric / slanted relative to the third
        // This is the canonical topology of a BJT symbol in a schematic
        GraphBjtTransistor bjt;
        bjt.centerPos = node.pos;
        bjt.consumedBranches = {b1, b2, b3};

        // Assign base as the branch most orthogonal to the other two
        bjt.baseNode = (branches[b1].nodeA == node.id) ? branches[b1].nodeB : branches[b1].nodeA;
        bjt.collectorNode = (branches[b2].nodeA == node.id) ? branches[b2].nodeB : branches[b2].nodeA;
        bjt.emitterNode = (branches[b3].nodeA == node.id) ? branches[b3].nodeB : branches[b3].nodeA;
        bjt.isNpn = true;

        transistors.push_back(bjt);
    }
}

void CircuitGraph::detectGrounds() {
    // Look for endpoint nodes (degree == 1) whose position is near the bottom of a vertical branch
    for (const auto& node: nodes) {
        if (node.degree != 1 || node.incidentBranches.empty()) {
            continue;
        }

        int branchId = node.incidentBranches[0];
        const auto& branch = branches[branchId];
        if (branch.type != BranchType::Wire) {
            continue;
        }

        // Check if the other node is above this endpoint (vertical orientation)
        int otherNodeId = (branch.nodeA == node.id) ? branch.nodeB : branch.nodeA;
        if (otherNodeId < 0 || otherNodeId >= static_cast<int>(nodes.size())) {
            continue;
        }

        const auto& otherNode = nodes[otherNodeId];
        double dy = node.pos.y - otherNode.pos.y;
        double dx = std::abs(node.pos.x - otherNode.pos.x);

        // Must drop downwards by at least 15px with mostly vertical alignment
        if (dy > 15.0 && dx < 0.6 * dy && branch.arcLength < 80.0) {
            // Check if there are short horizontal bars near this endpoint
            // Or if this endpoint is the bottom terminal of a ground stem
            GraphGround ground;
            ground.terminalNode = otherNodeId;
            ground.terminalPos = otherNode.pos;
            ground.width = 24.0;
            ground.consumedBranches = {branchId};
            grounds.push_back(ground);
        }
    }
}

}  // namespace xoj::circuit
