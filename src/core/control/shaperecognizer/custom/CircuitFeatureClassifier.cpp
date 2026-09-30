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

#include "CircuitFeatureClassifier.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "CircuitTemplate.h"

namespace xoj::circuit {

auto CircuitFeatureClassifier::extractFeatures(const Stroke* stroke) -> StrokeStructuralFeatures {
    StrokeStructuralFeatures feat;
    if (!stroke || stroke->getPointCount() < 4) {
        return feat;
    }

    const auto& pts = stroke->getPointVector();
    feat.chordStart = pts.front();
    feat.chordEnd = pts.back();

    double dx = feat.chordEnd.x - feat.chordStart.x;
    double dy = feat.chordEnd.y - feat.chordStart.y;
    feat.chordLength = std::hypot(dx, dy);

    if (feat.chordLength < 5.0) {
        return feat;
    }

    feat.chordAngle = std::atan2(dy, dx);
    double cosA = std::cos(-feat.chordAngle);
    double sinA = std::sin(-feat.chordAngle);

    // Compute arc length on raw points
    feat.arcLength = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        feat.arcLength += pts[i].lineLengthTo(pts[i + 1]);
    }
    feat.sinuosity = (feat.chordLength > 0.0) ? (feat.arcLength / feat.chordLength) : 1.0;

    // Resample to 64 equidistant points for uniform sampling density across all strokes
    auto sampledPts = CircuitTemplate::resampleEquidistant(pts, 64);
    if (sampledPts.size() < 16) {
        return feat;
    }

    std::vector<double> uVals;
    std::vector<double> vVals;
    uVals.reserve(sampledPts.size());
    vVals.reserve(sampledPts.size());

    for (const auto& p: sampledPts) {
        double px = p.x - feat.chordStart.x;
        double py = p.y - feat.chordStart.y;
        double u = (cosA * px - sinA * py) / feat.chordLength;
        double v = sinA * px + cosA * py;
        uVals.push_back(u);
        vVals.push_back(v);

        if (v > 0.0) {
            feat.maxPerpPositive = std::max(feat.maxPerpPositive, v);
        } else {
            feat.maxPerpNegative = std::max(feat.maxPerpNegative, -v);
        }
    }

    if (feat.chordLength < 15.0) {
        return feat;
    }

    // Smooth v values with 3-point moving average to filter tremor
    std::vector<double> vSmooth = vVals;
    for (size_t i = 1; i + 1 < vVals.size(); ++i) {
        vSmooth[i] = 0.25 * vVals[i - 1] + 0.50 * vVals[i] + 0.25 * vVals[i + 1];
    }

    // Minimum deviation threshold to count as a component tooth or coil bump
    double vThreshold = std::max(3.0, feat.chordLength * 0.04);

    // Collect candidate raw extrema
    std::vector<StrokeExtremum> rawExtrema;
    for (size_t i = 1; i + 1 < vSmooth.size(); ++i) {
        double vCurr = vSmooth[i];
        double vPrev = vSmooth[i - 1];
        double vNext = vSmooth[i + 1];

        if (vCurr > vPrev && vCurr >= vNext && vCurr >= vThreshold) {
            rawExtrema.push_back({i, uVals[i], vCurr, true});
        } else if (vCurr < vPrev && vCurr <= vNext && vCurr <= -vThreshold) {
            rawExtrema.push_back({i, uVals[i], vCurr, false});
        }
    }

    if (rawExtrema.empty()) {
        return feat;
    }

    // Compress consecutive extrema of identical sign unless separated by a clear return to baseline
    std::vector<StrokeExtremum> compressed;
    compressed.push_back(rawExtrema.front());

    for (size_t i = 1; i < rawExtrema.size(); ++i) {
        const auto& cand = rawExtrema[i];
        auto& last = compressed.back();

        if (cand.isPositive == last.isPositive) {
            // Check if there is an intervening return towards baseline between last.index and cand.index
            size_t idxStart = std::min(last.index, cand.index);
            size_t idxEnd = std::max(last.index, cand.index);
            bool separateBumps = false;

            if (cand.isPositive) {
                double minBetween = vSmooth[idxStart];
                for (size_t k = idxStart; k <= idxEnd; ++k) {
                    minBetween = std::min(minBetween, vSmooth[k]);
                }
                if (minBetween <= 0.40 * std::min(last.v, cand.v)) {
                    separateBumps = true;
                }
            } else {
                double maxBetween = vSmooth[idxStart];
                for (size_t k = idxStart; k <= idxEnd; ++k) {
                    maxBetween = std::max(maxBetween, vSmooth[k]);
                }
                if (maxBetween >= 0.40 * std::max(last.v, cand.v)) {
                    separateBumps = true;
                }
            }

            if (separateBumps) {
                compressed.push_back(cand);
            } else {
                if (cand.isPositive) {
                    if (cand.v > last.v) last = cand;
                } else {
                    if (cand.v < last.v) last = cand;
                }
            }
        } else {
            compressed.push_back(cand);
        }
    }

    feat.extrema = std::move(compressed);
    feat.alternatingExtremaCount = feat.extrema.size();

    for (const auto& ex: feat.extrema) {
        if (ex.isPositive) {
            feat.positivePeakCount++;
        } else {
            feat.negativeValleyCount++;
        }
    }

    // Check if motion is strictly or predominantly unipolar (bumps on one side, e.g. inductor)
    double maxPos = feat.maxPerpPositive;
    double maxNeg = feat.maxPerpNegative;
    if (maxPos > 0.0 || maxNeg > 0.0) {
        if (maxNeg <= 0.25 * maxPos && feat.positivePeakCount >= 2) {
            feat.isUnipolar = true;
        } else if (maxPos <= 0.25 * maxNeg && feat.negativeValleyCount >= 2) {
            feat.isUnipolar = true;
        }
    }

    return feat;
}

auto CircuitFeatureClassifier::classify(const Stroke* stroke) -> CircuitFeatureClass {
    if (!stroke || stroke->getPointCount() < 4) {
        return CircuitFeatureClass::Unknown;
    }

    auto feat = extractFeatures(stroke);

    // 1. Resistor (IEEE Zig-Zag):
    // Requires alternating positive peaks and negative valleys across the baseline
    if (feat.alternatingExtremaCount >= 3 || (feat.positivePeakCount >= 2 && feat.negativeValleyCount >= 2)) {
        double maxDev = std::max(feat.maxPerpPositive, feat.maxPerpNegative);
        double minDev = std::min(feat.maxPerpPositive, feat.maxPerpNegative);

        // Verify bipolar symmetry (teeth extend significantly on both sides of wire)
        if (maxDev >= 3.5 && (minDev / maxDev) >= 0.20 && feat.sinuosity >= 1.15) {
            return CircuitFeatureClass::ResistorIeee;
        }
    }

    // 2. Inductor (Unipolar Coils):
    // Requires multiple peaks strictly on one side of baseline
    if (feat.isUnipolar && (feat.positivePeakCount >= 2 || feat.negativeValleyCount >= 2) && feat.sinuosity >= 1.15) {
        return CircuitFeatureClass::Inductor;
    }

    // 3. Straight Wire:
    // Minimal perpendicular deviation and zero alternating extrema
    double maxPerp = std::max(feat.maxPerpPositive, feat.maxPerpNegative);
    if (maxPerp <= std::max(8.0, feat.chordLength * 0.10) && feat.alternatingExtremaCount == 0 && feat.sinuosity <= 1.12) {
        return CircuitFeatureClass::StraightWire;
    }

    return CircuitFeatureClass::Unknown;
}

}  // namespace xoj::circuit
