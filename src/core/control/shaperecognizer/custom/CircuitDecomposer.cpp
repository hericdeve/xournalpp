#include "CircuitDecomposer.h"

#include <algorithm>
#include <cmath>
#include <queue>

#include "CircuitFeatureClassifier.h"
#include "CircuitTemplate.h"

namespace xoj::circuit {

auto CircuitDecomposer::clusterTextBlocks(const std::vector<Stroke*>& candidates) -> std::set<Stroke*> {
    std::set<Stroke*> protectedStrokes;
    if (candidates.empty()) {
        return protectedStrokes;
    }

    // Step 1: Gather candidate text/character strokes
    std::vector<Stroke*> charCandidates;
    for (Stroke* s: candidates) {
        if (!s || s->getPointCount() < 2) continue;

        if (CircuitFeatureClassifier::isHandwritingOrAnnotation(s)) {
            charCandidates.push_back(s);
            continue;
        }

        auto box = s->getBoundingBox();
        double maxDim = std::max(box.width, box.height);

        // Characters, digits (like '1', '7', 'I', '-'), and annotations typically fit in a 45x45 box
        if (maxDim <= 45.0) {
            auto feat = CircuitFeatureClassifier::extractFeatures(s);
            if (feat.chordLength <= 45.0) {
                charCandidates.push_back(s);
            }
        }
    }

    if (charCandidates.empty()) {
        return protectedStrokes;
    }

    // Step 2: Build adjacency graph based on Euclidean bounding box distance
    const size_t n = charCandidates.size();
    std::vector<std::vector<size_t>> adj(n);
    constexpr double TEXT_PROXIMITY = 22.0;

    for (size_t i = 0; i < n; ++i) {
        auto boxA = charCandidates[i]->getBoundingBox();
        for (size_t j = i + 1; j < n; ++j) {
            auto boxB = charCandidates[j]->getBoundingBox();

            double dx = 0.0;
            if (boxA.x > boxB.x + boxB.width) {
                dx = boxA.x - (boxB.x + boxB.width);
            } else if (boxB.x > boxA.x + boxA.width) {
                dx = boxB.x - (boxA.x + boxA.width);
            }

            double dy = 0.0;
            if (boxA.y > boxB.y + boxB.height) {
                dy = boxA.y - (boxB.y + boxB.height);
            } else if (boxB.y > boxA.y + boxA.height) {
                dy = boxB.y - (boxA.y + boxA.height);
            }

            double dist = std::hypot(dx, dy);
            if (dist <= TEXT_PROXIMITY) {
                adj[i].push_back(j);
                adj[j].push_back(i);
            }
        }
    }

    // Step 3: Find connected components and mark all cluster strokes
    std::vector<bool> visited(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (visited[i]) continue;

        std::vector<size_t> component;
        std::queue<size_t> q;
        q.push(i);
        visited[i] = true;

        while (!q.empty()) {
            size_t u = q.front();
            q.pop();
            component.push_back(u);

            for (size_t v: adj[u]) {
                if (!visited[v]) {
                    visited[v] = true;
                    q.push(v);
                }
            }
        }

        if (component.size() >= 2) {
            // Multi-character word or number cluster ("R1", "22K", "RE", "1,5", "RC", "2K", "34K"):
            // Protect all strokes in this text cluster (including commas and decimal points)
            for (size_t idx: component) {
                protectedStrokes.insert(charCandidates[idx]);
            }
        } else if (component.size() == 1) {
            Stroke* s = charCandidates[component[0]];
            auto bbox = s->getBoundingBox();
            double diag = std::hypot(bbox.width, bbox.height);

            // Single isolated stroke: protect if it is a character or cursive annotation
            if (diag > 22.0) {
                if (CircuitFeatureClassifier::isHandwritingOrAnnotation(s)) {
                    protectedStrokes.insert(s);
                }
            } else {
                // For tiny strokes (diag <= 22.0), only protect if it has cursive sinuosity.
                // Simple dots or small loops are left available as candidate circuit node markers.
                auto feat = CircuitFeatureClassifier::extractFeatures(s);
                if (feat.sinuosity > 1.35) {
                    protectedStrokes.insert(s);
                }
            }
        }
    }

    return protectedStrokes;
}

auto CircuitDecomposer::computeAverageVertexAngle(const Stroke* stroke) -> double {
    if (!stroke || stroke->getPointCount() < 4) {
        return 180.0;
    }

    const auto& origPts = stroke->getPointVector();
    double arcLen = 0.0;
    for (size_t i = 0; i + 1 < origPts.size(); ++i) {
        arcLen += origPts[i].lineLengthTo(origPts[i + 1]);
    }
    if (arcLen < 15.0) {
        return 180.0;
    }

    // Resample with ~2px dense spacing
    size_t sampleCount = std::clamp(static_cast<size_t>(arcLen / 2.0), size_t(32), size_t(256));
    auto pts = CircuitTemplate::resampleEquidistant(origPts, sampleCount);
    if (pts.size() < 12) {
        return 180.0;
    }

    // Step size for vertex angle computation (approx 4.0 - 5.0 px)
    const size_t k = std::clamp(static_cast<size_t>(pts.size() / 24), size_t(2), size_t(5));

    std::vector<double> angles;
    for (size_t i = k; i + k < pts.size(); ++i) {
        Point u(pts[i - k].x - pts[i].x, pts[i - k].y - pts[i].y);
        Point v(pts[i + k].x - pts[i].x, pts[i + k].y - pts[i].y);

        double lenU = std::hypot(u.x, u.y);
        double lenV = std::hypot(v.x, v.y);
        if (lenU < 1e-4 || lenV < 1e-4) continue;

        double dot = (u.x * v.x + u.y * v.y) / (lenU * lenV);
        dot = std::clamp(dot, -1.0, 1.0);
        double angleDeg = std::acos(dot) * (180.0 / M_PI);

        // A vertex represents a turn if angle is acute to moderate (< 140 deg)
        if (angleDeg < 140.0) {
            angles.push_back(angleDeg);
        }
    }

    if (angles.empty()) {
        return 180.0;
    }

    // Return the average of the sharpest 50% turns
    std::sort(angles.begin(), angles.end());
    size_t count = std::max(size_t(1), angles.size() / 2);
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += angles[i];
    }
    return sum / static_cast<double>(count);
}

auto CircuitDecomposer::decomposeCompoundStroke(const Stroke* stroke) -> std::vector<DecomposedStrokePart> {
    std::vector<DecomposedStrokePart> parts;
    if (!stroke || stroke->getPointCount() < 8) {
        return parts;
    }

    auto feat = CircuitFeatureClassifier::extractFeatures(stroke);
    if (!feat.hasOscillatingBody || feat.alternatingExtremaCount < 2) {
        return parts;
    }

    // Require genuine zig-zag teeth in the oscillating body
    if (feat.bodySinuosity < 1.12) {
        return parts;
    }

    const auto& origPts = stroke->getPointVector();
    double totalArc = feat.arcLength;
    if (totalArc < 30.0) {
        return parts;
    }

    // Resample equidistant points to precisely match feature indices
    size_t sampleCount = std::clamp(static_cast<size_t>(feat.arcLength / 2.0), size_t(64), size_t(256));
    auto sampledPts = CircuitTemplate::resampleEquidistant(origPts, sampleCount);
    if (sampledPts.size() < 16) {
        return parts;
    }

    size_t bStartIdx = feat.extrema.front().index;
    size_t bEndIdx = feat.extrema.back().index;

    // Safety bounds
    if (bStartIdx >= sampledPts.size() || bEndIdx >= sampledPts.size() || bStartIdx >= bEndIdx) {
        return parts;
    }

    // Check leading straight wire lead
    double lead1Dist = sampledPts.front().lineLengthTo(sampledPts[bStartIdx]);
    // Check trailing straight wire lead
    double lead2Dist = sampledPts[bEndIdx].lineLengthTo(sampledPts.back());

    // Only split if at least one lead has significant length (>= 12px)
    if (lead1Dist < 12.0 && lead2Dist < 12.0) {
        return parts;
    }

    // Build Lead 1
    if (lead1Dist >= 10.0 && bStartIdx >= 3) {
        auto sLead1 = std::make_unique<Stroke>();
        sLead1->applyStyleFrom(stroke);
        std::vector<Point> pList;
        for (size_t i = 0; i <= bStartIdx; ++i) {
            pList.push_back(sampledPts[i]);
        }
        sLead1->setPointVector(std::move(pList));
        parts.push_back({std::move(sLead1), false, true});
    }

    // Build Resistor Body
    {
        auto sBody = std::make_unique<Stroke>();
        sBody->applyStyleFrom(stroke);
        std::vector<Point> pList;
        for (size_t i = bStartIdx; i <= bEndIdx; ++i) {
            pList.push_back(sampledPts[i]);
        }
        sBody->setPointVector(std::move(pList));
        parts.push_back({std::move(sBody), true, false});
    }

    // Build Lead 2
    if (lead2Dist >= 10.0 && bEndIdx + 3 <= sampledPts.size()) {
        auto sLead2 = std::make_unique<Stroke>();
        sLead2->applyStyleFrom(stroke);
        std::vector<Point> pList;
        for (size_t i = bEndIdx; i < sampledPts.size(); ++i) {
            pList.push_back(sampledPts[i]);
        }
        sLead2->setPointVector(std::move(pList));
        parts.push_back({std::move(sLead2), false, true});
    }

    return parts;
}

auto CircuitDecomposer::splitStrokeAtPoints(const Stroke* stroke,
                                            const std::vector<Point>& splitPoints,
                                            double maxDist,
                                            double minEndpointDist)
        -> std::vector<std::unique_ptr<Stroke>> {
    std::vector<std::unique_ptr<Stroke>> result;
    if (!stroke || stroke->getPointCount() < 2 || splitPoints.empty()) {
        return result;
    }

    const auto& pts = stroke->getPointVector();

    // Check if stroke is relatively straight wire rather than oscillating component
    auto feat = CircuitFeatureClassifier::extractFeatures(stroke);
    if (feat.sinuosity > 1.25 || feat.alternatingExtremaCount > 0) {
        return result; // Do not split components
    }

    struct SplitCandidate {
        size_t segIdx;
        Point proj;
        double distAlongStroke;
    };
    std::vector<SplitCandidate> candidates;

    // Cumulative length array
    std::vector<double> cumLen(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i) {
        cumLen[i] = cumLen[i - 1] + pts[i - 1].lineLengthTo(pts[i]);
    }
    double totalLen = cumLen.back();

    for (const auto& sp: splitPoints) {
        double bestDist = maxDist;
        size_t bestSeg = 0;
        Point bestProj;
        bool found = false;

        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            Point a = pts[i];
            Point b = pts[i + 1];
            double segDx = b.x - a.x;
            double segDy = b.y - a.y;
            double segLenSq = segDx * segDx + segDy * segDy;
            if (segLenSq < 1e-4) continue;

            double t = std::clamp(((sp.x - a.x) * segDx + (sp.y - a.y) * segDy) / segLenSq, 0.0, 1.0);
            Point proj(a.x + t * segDx, a.y + t * segDy);
            double d = sp.lineLengthTo(proj);

            if (d <= bestDist) {
                double distAlong = cumLen[i] + a.lineLengthTo(proj);
                if (distAlong >= minEndpointDist && (totalLen - distAlong) >= minEndpointDist) {
                    bestDist = d;
                    bestSeg = i;
                    bestProj = proj;
                    found = true;
                }
            }
        }

        if (found) {
            double distAlong = cumLen[bestSeg] + pts[bestSeg].lineLengthTo(bestProj);
            candidates.push_back({bestSeg, bestProj, distAlong});
        }
    }

    if (candidates.empty()) {
        return result;
    }

    // Sort candidates along the stroke
    std::sort(candidates.begin(), candidates.end(), [](const SplitCandidate& a, const SplitCandidate& b) {
        return a.distAlongStroke < b.distAlongStroke;
    });

    // Remove duplicates that are too close to each other
    std::vector<SplitCandidate> uniqueSplits;
    for (const auto& c: candidates) {
        if (uniqueSplits.empty() || (c.distAlongStroke - uniqueSplits.back().distAlongStroke) >= minEndpointDist) {
            uniqueSplits.push_back(c);
        }
    }

    // Build sub-stroke segments
    size_t curPtIdx = 0;
    for (size_t sIdx = 0; sIdx <= uniqueSplits.size(); ++sIdx) {
        auto sub = std::make_unique<Stroke>();
        sub->applyStyleFrom(stroke);

        if (sIdx == 0) {
            // From pts[0] up to uniqueSplits[0].proj
            for (size_t i = 0; i <= uniqueSplits[0].segIdx; ++i) {
                sub->addPoint(pts[i]);
            }
            sub->addPoint(uniqueSplits[0].proj);
            curPtIdx = uniqueSplits[0].segIdx + 1;
        } else if (sIdx == uniqueSplits.size()) {
            // From uniqueSplits.back().proj to pts.back()
            sub->addPoint(uniqueSplits.back().proj);
            for (size_t i = curPtIdx; i < pts.size(); ++i) {
                sub->addPoint(pts[i]);
            }
        } else {
            // From uniqueSplits[sIdx-1].proj to uniqueSplits[sIdx].proj
            sub->addPoint(uniqueSplits[sIdx - 1].proj);
            for (size_t i = curPtIdx; i <= uniqueSplits[sIdx].segIdx; ++i) {
                sub->addPoint(pts[i]);
            }
            sub->addPoint(uniqueSplits[sIdx].proj);
            curPtIdx = uniqueSplits[sIdx].segIdx + 1;
        }

        if (sub->getPointCount() >= 2) {
            result.push_back(std::move(sub));
        }
    }

    return result;
}

}  // namespace xoj::circuit
