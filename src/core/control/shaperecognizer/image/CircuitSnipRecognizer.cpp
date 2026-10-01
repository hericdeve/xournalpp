#include "CircuitSnipRecognizer.h"

#include <cairo.h>

#include <algorithm>
#include <cmath>

#include "control/shaperecognizer/custom/CircuitDecomposer.h"
#include "control/shaperecognizer/custom/CircuitFeatureClassifier.h"
#include "control/shaperecognizer/custom/CircuitSnapper.h"

namespace xoj::circuit {

namespace {

auto distance(const Point& a, const Point& b) -> double {
    return std::hypot(a.x - b.x, a.y - b.y);
}

}  // namespace

auto CircuitSnipRecognizer::rasterizeStrokes(const std::vector<Stroke*>& strokes,
                                             double minX, double minY, int width, int height,
                                             double padding) -> BinaryGrid {
    BinaryGrid grid(width, height);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        return grid;
    }

    cairo_t* cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 2.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    for (const auto* s: strokes) {
        if (!s) {
            continue;
        }
        const auto& pts = s->getPointVector();
        if (pts.empty()) {
            continue;
        }

        cairo_move_to(cr, pts[0].x - minX + padding, pts[0].y - minY + padding);
        for (size_t i = 1; i < pts.size(); ++i) {
            cairo_line_to(cr, pts[i].x - minX + padding, pts[i].y - minY + padding);
        }
        cairo_stroke(cr);
    }

    cairo_surface_flush(surface);
    int stride = cairo_image_surface_get_stride(surface);
    const unsigned char* pixelData = cairo_image_surface_get_data(surface);

    for (int y = 0; y < height; ++y) {
        const unsigned char* row = pixelData + y * stride;
        for (int x = 0; x < width; ++x) {
            if (row[x] > 90) {
                grid.set(x, y, 1);
            }
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return grid;
}

auto CircuitSnipRecognizer::processSnip(const std::vector<Stroke*>& selectedStrokes,
                                        CustomShapeManager* shapeManager) -> CircuitSnipResult {
    CircuitSnipResult result;
    if (selectedStrokes.empty() || !shapeManager) {
        return result;
    }

    // 1. Separate text and annotation strokes
    std::vector<Stroke*> circuitStrokes;
    for (auto* s: selectedStrokes) {
        if (!s) continue;
        if (CircuitFeatureClassifier::isHandwritingOrAnnotation(s)) {
            result.protectedTextStrokes.push_back(s);
        } else {
            circuitStrokes.push_back(s);
        }
    }

    if (circuitStrokes.empty()) {
        return result;
    }

    // Also run TextBlock clustering to catch compound labels like "22K"
    auto textClusters = CircuitDecomposer::clusterTextBlocks(circuitStrokes);
    for (auto* s: textClusters) {
        result.protectedTextStrokes.push_back(s);
        auto it = std::find(circuitStrokes.begin(), circuitStrokes.end(), s);
        if (it != circuitStrokes.end()) {
            circuitStrokes.erase(it);
        }
    }

    if (circuitStrokes.empty()) {
        return result;
    }

    // 2. Compute bounding box of circuit ink
    double minX = 1e9;
    double minY = 1e9;
    double maxX = -1e9;
    double maxY = -1e9;

    for (const auto* s: circuitStrokes) {
        for (const auto& pt: s->getPointVector()) {
            minX = std::min(minX, pt.x);
            maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y);
            maxY = std::max(maxY, pt.y);
        }
    }

    double padding = 20.0;
    int gridW = std::max(1, static_cast<int>(std::ceil(maxX - minX + 2 * padding)));
    int gridH = std::max(1, static_cast<int>(std::ceil(maxY - minY + 2 * padding)));

    // 3. Rasterize circuit strokes to BinaryGrid
    BinaryGrid grid = rasterizeStrokes(circuitStrokes, minX, minY, gridW, gridH, padding);

    // 4. Zhang-Suen morphological skeletonization
    ZhangSuenThinner::thin(grid);

    // 5. Extract topological circuit graph
    CircuitGraph graph = CircuitGraph::extractFromSkeleton(grid);

    const auto& nodes = graph.getNodes();
    const auto& branches = graph.getBranches();
    if (nodes.empty() || branches.empty()) {
        return result;
    }

    auto toWorldPt = [&](const Point& p) -> Point {
        return Point(minX - padding + p.x, minY - padding + p.y);
    };

    const Stroke* styleSource = circuitStrokes.front();
    auto* resistorTpl = shapeManager->getTemplateById("resistor_ieee");
    auto* inductorTpl = shapeManager->getTemplateById("inductor");
    auto* groundTpl = shapeManager->getTemplateById("ground");

    std::vector<bool> branchConsumed(branches.size(), false);

    // 6. Synthesize Grounds
    for (const auto& g: graph.getGrounds()) {
        if (!groundTpl) continue;
        Point worldTerm = toWorldPt(g.terminalPos);
        auto groundStrokes = CircuitSnapper::snapCircuitComposite(groundTpl, worldTerm, Point(worldTerm.x, worldTerm.y + 24.0),
                                                                 styleSource, true);
        for (auto& s: groundStrokes) {
            result.strokesToInsert.push_back(std::move(s));
        }
        for (int bid: g.consumedBranches) {
            if (bid >= 0 && bid < static_cast<int>(branchConsumed.size())) {
                branchConsumed[bid] = true;
            }
        }
    }

    // 6.5. Synthesize BJT Transistors
    auto* npnTpl = shapeManager->getTemplateById("transistor_npn");
    auto* pnpTpl = shapeManager->getTemplateById("transistor_pnp");
    for (const auto& bjt: graph.getTransistors()) {
        auto* tpl = bjt.isNpn ? npnTpl : pnpTpl;
        if (!tpl) continue;
        
        Point wCenter = toWorldPt(bjt.centerPos);
        Point wBase = toWorldPt(nodes[bjt.baseNode].pos);
        Point wCol = toWorldPt(nodes[bjt.collectorNode].pos);
        Point wEmi = toWorldPt(nodes[bjt.emitterNode].pos);
        
        // Compute standard bounding box size for the BJT
        double r = 25.0; 
        Point b1(wCenter.x - r, wCenter.y - r);
        Point b2(wCenter.x + r, wCenter.y + r);
        
        auto bjtStrokes = CircuitSnapper::snapCircuitComposite(tpl, b1, b2, styleSource, true);
        for (auto& s: bjtStrokes) {
            result.strokesToInsert.push_back(std::move(s));
        }
        
        // Draw straight wires from the component pins to the extracted graph nodes
        auto drawLead = [&](const Point& p1, const Point& p2) {
            auto lead = std::make_unique<Stroke>();
            lead->setColor(styleSource->getColor());
            lead->setWidth(styleSource->getWidth());
            lead->setLineStyle(styleSource->getLineStyle());
            lead->setToolType(styleSource->getToolType());
            lead->addPoint(p1);
            lead->addPoint(p2);
            result.strokesToInsert.push_back(std::move(lead));
        };
        
        drawLead(Point(wCenter.x - 12.0, wCenter.y), wBase);
        drawLead(Point(wCenter.x + 12.0, wCenter.y - 18.0), wCol);
        drawLead(Point(wCenter.x + 12.0, wCenter.y + 18.0), wEmi);
        
        for (int bid: bjt.consumedBranches) {
            if (bid >= 0 && bid < static_cast<int>(branchConsumed.size())) {
                branchConsumed[bid] = true;
            }
        }
    }

    // 7. Synthesize Components (Resistors & Inductors)
    for (size_t bid = 0; bid < branches.size(); ++bid) {
        if (branchConsumed[bid]) continue;
        const auto& branch = branches[bid];

        if (branch.nodeA < 0 || branch.nodeA >= static_cast<int>(nodes.size()) ||
            branch.nodeB < 0 || branch.nodeB >= static_cast<int>(nodes.size())) {
            continue;
        }

        Point pA = toWorldPt(nodes[branch.nodeA].pos);
        Point pB = toWorldPt(nodes[branch.nodeB].pos);

        if (branch.type == BranchType::Resistor && resistorTpl) {
            auto rStroke = CircuitSnapper::snapCircuit(resistorTpl, pA, pB, styleSource, true);
            if (rStroke) {
                result.strokesToInsert.push_back(std::move(rStroke));
                branchConsumed[bid] = true;
            }
        } else if (branch.type == BranchType::Inductor && inductorTpl) {
            auto indStroke = CircuitSnapper::snapCircuit(inductorTpl, pA, pB, styleSource, true);
            if (indStroke) {
                result.strokesToInsert.push_back(std::move(indStroke));
                branchConsumed[bid] = true;
            }
        }
    }

    // 7.5. Synthesize Circles / Loops (AC Sources)
    for (size_t bid = 0; bid < branches.size(); ++bid) {
        if (branchConsumed[bid] || branches[bid].id == -1) continue;
        const auto& branch = branches[bid];
        
        if (branch.nodeA == branch.nodeB && branch.chordLength < 10.0 && branch.pixelPath.size() > 50) {
            // Closed loop
            double cx = 0, cy = 0;
            for (const auto& pt : branch.pixelPath) {
                cx += pt.first;
                cy += pt.second;
            }
            double nPts = static_cast<double>(branch.pixelPath.size());
            cx /= nPts;
            cy /= nPts;
            
            double r = 0;
            for (const auto& pt : branch.pixelPath) {
                r += std::hypot(pt.first - cx, pt.second - cy);
            }
            r /= nPts;
            
            if (r > 10.0) {
                Point center = toWorldPt(Point(cx, cy));
                
                auto circle = std::make_unique<Stroke>();
                circle->setColor(styleSource->getColor());
                circle->setWidth(styleSource->getWidth());
                circle->setLineStyle(styleSource->getLineStyle());
                circle->setToolType(styleSource->getToolType());
                
                int numPts = 60;
                for (int i = 0; i <= numPts; ++i) {
                    double angle = i * 2.0 * M_PI / numPts;
                    circle->addPoint(Point(center.x + r * std::cos(angle), center.y + r * std::sin(angle)));
                }
                
                result.strokesToInsert.push_back(std::move(circle));
                branchConsumed[bid] = true;
            }
        }
    }

    // 8. Synthesize Interconnecting Wires (Manhattan 0/90 deg)
    for (size_t bid = 0; bid < branches.size(); ++bid) {
        if (branchConsumed[bid] || branches[bid].id == -1) continue;
        const auto& branch = branches[bid];

        if (branch.nodeA < 0 || branch.nodeA >= static_cast<int>(nodes.size()) ||
            branch.nodeB < 0 || branch.nodeB >= static_cast<int>(nodes.size())) {
            continue;
        }

        Point pA = toWorldPt(nodes[branch.nodeA].pos);
        Point pB = toWorldPt(nodes[branch.nodeB].pos);

        // Arrowhead synthesis
        if (branch.arcLength < 18.0 && (nodes[branch.nodeA].degree == 1 || nodes[branch.nodeB].degree == 1)) {
            int tipNodeId = (nodes[branch.nodeA].degree == 1) ? branch.nodeA : branch.nodeB;
            int juncNodeId = (tipNodeId == branch.nodeA) ? branch.nodeB : branch.nodeA;
            
            if (nodes[juncNodeId].degree == 3) {
                // Check if this is one wing of an arrowhead
                std::vector<int> wingBranches;
                for (int adjBid : nodes[juncNodeId].incidentBranches) {
                    if (adjBid == -1 || adjBid >= static_cast<int>(branches.size())) continue;
                    const auto& adjBranch = branches[adjBid];
                    if (adjBranch.arcLength < 18.0) {
                        int otherNode = (adjBranch.nodeA == juncNodeId) ? adjBranch.nodeB : adjBranch.nodeA;
                        if (nodes[otherNode].degree == 1) {
                            wingBranches.push_back(adjBid);
                        }
                    }
                }
                
                if (wingBranches.size() >= 2) {
                    for (int wBid : wingBranches) {
                        branchConsumed[wBid] = true;
                    }
                    continue; // we just consume them, they'll be drawn as part of a straight wire
                }
            }
        }

        if (distance(pA, pB) < 3.0) {
            continue;
        }

        auto wire = std::make_unique<Stroke>();
        wire->setColor(styleSource->getColor());
        wire->setWidth(styleSource->getWidth());
        wire->setLineStyle(styleSource->getLineStyle());
        wire->setToolType(styleSource->getToolType());

        double dx = std::abs(pA.x - pB.x);
        double dy = std::abs(pA.y - pB.y);

        if (dx < 10.0) {
            // Pure vertical line
            wire->addPoint(Point(pA.x, pA.y));
            wire->addPoint(Point(pA.x, pB.y));
        } else if (dy < 10.0) {
            // Pure horizontal line
            wire->addPoint(Point(pA.x, pA.y));
            wire->addPoint(Point(pB.x, pA.y));
        } else {
            // L-corner wire
            Point mid((pA.x + pB.x) / 2.0, (pA.y + pB.y) / 2.0);
            if (!branch.pixelPath.empty()) {
                auto midPixel = branch.pixelPath[branch.pixelPath.size() / 2];
                mid = toWorldPt(Point(midPixel.first, midPixel.second));
            }

            // Strictly orthogonal step
            Point corner1(pB.x, pA.y);
            Point corner2(pA.x, pB.y);

            Point chosenCorner = (distance(mid, corner1) < distance(mid, corner2)) ? corner1 : corner2;

            wire->addPoint(pA);
            wire->addPoint(chosenCorner);
            wire->addPoint(pB);
        }

        result.strokesToInsert.push_back(std::move(wire));
        branchConsumed[bid] = true;
    }

    if (!result.strokesToInsert.empty()) {
        result.strokesToRemove = circuitStrokes;
        result.success = true;
    }

    return result;
}

}  // namespace xoj::circuit
