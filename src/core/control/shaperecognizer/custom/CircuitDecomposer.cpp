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
        double minDim = std::min(box.width, box.height);
        double aspect = (minDim > 0.0) ? (maxDim / minDim) : 100.0;

        // Characters typically fit in a 42x42 box with moderate aspect ratio
        if (maxDim <= 42.0 && aspect <= 3.0) {
            auto feat = CircuitFeatureClassifier::extractFeatures(s);
            if (feat.chordLength <= 40.0) {
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

        // Check if component has at least one stroke exhibiting handwriting characteristics,
        // or is a single character stroke
        bool hasHandwriting = false;
        for (size_t idx: component) {
            if (CircuitFeatureClassifier::isHandwritingOrAnnotation(charCandidates[idx])) {
                hasHandwriting = true;
                break;
            }
        }

        if (hasHandwriting || component.size() >= 2) {
            for (size_t idx: component) {
                protectedStrokes.insert(charCandidates[idx]);
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
    return sum / count;
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

}  // namespace xoj::circuit
