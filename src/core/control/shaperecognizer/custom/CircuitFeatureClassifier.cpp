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

#include "CircuitDecomposer.h"
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

    // Resample with dense equidistant sampling (every 1.5 - 2.0 px) so no sharp teeth are missed
    size_t sampleCount = std::clamp(static_cast<size_t>(feat.arcLength / 2.0), size_t(64), size_t(256));
    auto sampledPts = CircuitTemplate::resampleEquidistant(pts, sampleCount);
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

    // 1. Find all raw local extrema (peaks and valleys of vSmooth)
    struct RawExtremum {
        size_t index;
        double u;
        double v;
        bool isPeak;
    };
    std::vector<RawExtremum> rawExtrema;
    for (size_t i = 1; i + 1 < vSmooth.size(); ++i) {
        if (vSmooth[i] > vSmooth[i - 1] && vSmooth[i] >= vSmooth[i + 1]) {
            rawExtrema.push_back({i, uVals[i], vSmooth[i], true});
        } else if (vSmooth[i] < vSmooth[i - 1] && vSmooth[i] <= vSmooth[i + 1]) {
            rawExtrema.push_back({i, uVals[i], vSmooth[i], false});
        }
    }

    // 2. Filter extrema using peak-to-peak amplitude hysteresis
    // Minimum amplitude between a peak and valley to count as an intentional tooth/oscillation
    constexpr double minAmp = 2.5;
    std::vector<StrokeExtremum> significant;

    for (const auto& ex: rawExtrema) {
        if (significant.empty()) {
            significant.push_back({ex.index, ex.u, ex.v, ex.isPeak});
        } else {
            auto& last = significant.back();
            if (ex.isPeak == last.isPositive) {
                // Same direction: keep the more extreme point
                if (ex.isPeak && ex.v > last.v) {
                    last = {ex.index, ex.u, ex.v, ex.isPeak};
                } else if (!ex.isPeak && ex.v < last.v) {
                    last = {ex.index, ex.u, ex.v, ex.isPeak};
                }
            } else {
                // Opposite direction: require significant transition amplitude
                if (std::abs(ex.v - last.v) >= minAmp) {
                    significant.push_back({ex.index, ex.u, ex.v, ex.isPeak});
                }
            }
        }
    }

    feat.extrema = std::move(significant);
    feat.alternatingExtremaCount = feat.extrema.size();

    for (const auto& ex: feat.extrema) {
        if (ex.isPositive && ex.v >= 1.5) {
            feat.positivePeakCount++;
        } else if (!ex.isPositive && ex.v <= -1.5) {
            feat.negativeValleyCount++;
        }
    }

    // If alternating extrema exist, identify active oscillating body range
    if (feat.alternatingExtremaCount >= 2) {
        feat.bodyStartU = std::clamp(feat.extrema.front().u - 0.03, 0.0, 1.0);
        feat.bodyEndU = std::clamp(feat.extrema.back().u + 0.03, 0.0, 1.0);
        size_t bStart = feat.extrema.front().index;
        size_t bEnd = feat.extrema.back().index;
        double bodyChord = sampledPts[bStart].lineLengthTo(sampledPts[bEnd]);
        if (bodyChord < 1.0) {
            bodyChord = 1.0;
        }
        double bodyArc = 0.0;
        for (size_t k = bStart; k < bEnd && k + 1 < sampledPts.size(); ++k) {
            bodyArc += sampledPts[k].lineLengthTo(sampledPts[k + 1]);
        }
        feat.bodySinuosity = (bodyChord > 0.0) ? (bodyArc / bodyChord) : 1.0;
        feat.hasOscillatingBody = true;
    }

    // Check if motion is strictly unipolar (e.g. inductor coils)
    double maxPos = feat.maxPerpPositive;
    double maxNeg = feat.maxPerpNegative;
    if (maxPos > 0.0 || maxNeg > 0.0) {
        if (maxNeg <= 0.20 * maxPos && feat.positivePeakCount >= 2) {
            feat.isUnipolar = true;
        } else if (maxPos <= 0.20 * maxNeg && feat.negativeValleyCount >= 2) {
            feat.isUnipolar = true;
        }
    }

    return feat;
}

auto CircuitFeatureClassifier::detectArrow(const Stroke* stroke, Point& outShaftStart, Point& outTip) -> bool {
    if (!stroke || stroke->getPointCount() < 4) {
        return false;
    }

    const auto& pts = stroke->getPointVector();
    const size_t n = pts.size();

    double totalArc = 0.0;
    for (size_t i = 0; i + 1 < n; ++i) {
        totalArc += pts[i].lineLengthTo(pts[i + 1]);
    }
    if (totalArc < 18.0) {
        return false;
    }

    // Case 1: Shaft first, then arrowhead at the end (start at pts[0], tip at max distance)
    const Point& p0 = pts.front();
    double maxDist0 = 0.0;
    size_t tipIdx0 = 0;
    for (size_t i = 1; i < n; ++i) {
        double d = pts[i].lineLengthTo(p0);
        if (d > maxDist0) {
            maxDist0 = d;
            tipIdx0 = i;
        }
    }

    if (maxDist0 >= 16.0 && tipIdx0 >= 1) {
        double shaftArc0 = 0.0;
        for (size_t i = 0; i < tipIdx0; ++i) {
            shaftArc0 += pts[i].lineLengthTo(pts[i + 1]);
        }
        // Shaft is roughly straight
        if (shaftArc0 / maxDist0 <= 1.25) {
            // Points after tipIdx0 form the arrowhead
            double headArc0 = 0.0;
            for (size_t i = tipIdx0; i + 1 < n; ++i) {
                headArc0 += pts[i].lineLengthTo(pts[i + 1]);
            }
            if (headArc0 >= 4.0 && headArc0 <= std::max(35.0, maxDist0 * 0.75)) {
                bool headNearTip = true;
                for (size_t i = tipIdx0 + 1; i < n; ++i) {
                    if (pts[i].lineLengthTo(pts[tipIdx0]) > std::max(20.0, maxDist0 * 0.55)) {
                        headNearTip = false;
                        break;
                    }
                }
                if (headNearTip) {
                    outShaftStart = p0;
                    outTip = pts[tipIdx0];
                    return true;
                }
            }
        }
    }

    // Case 2: Arrowhead first, then shaft to pts.back()
    const Point& pEnd = pts.back();
    double maxDistEnd = 0.0;
    size_t tipIdxEnd = 0;
    for (size_t i = 0; i + 1 < n; ++i) {
        double d = pts[i].lineLengthTo(pEnd);
        if (d > maxDistEnd) {
            maxDistEnd = d;
            tipIdxEnd = i;
        }
    }

    if (maxDistEnd >= 16.0 && tipIdxEnd + 1 <= n) {
        double shaftArcEnd = 0.0;
        for (size_t i = tipIdxEnd; i + 1 < n; ++i) {
            shaftArcEnd += pts[i].lineLengthTo(pts[i + 1]);
        }
        if (shaftArcEnd / maxDistEnd <= 1.25) {
            double headArcEnd = 0.0;
            for (size_t i = 0; i < tipIdxEnd; ++i) {
                headArcEnd += pts[i].lineLengthTo(pts[i + 1]);
            }
            if (headArcEnd >= 4.0 && headArcEnd <= std::max(28.0, maxDistEnd * 0.65)) {
                bool headNearTip = true;
                for (size_t i = 0; i < tipIdxEnd; ++i) {
                    if (pts[i].lineLengthTo(pts[tipIdxEnd]) > std::max(18.0, maxDistEnd * 0.45)) {
                        headNearTip = false;
                        break;
                    }
                }
                if (headNearTip) {
                    outShaftStart = pEnd;
                    outTip = pts[tipIdxEnd];
                    return true;
                }
            }
        }
    }

    return false;
}

auto CircuitFeatureClassifier::detectSingleStrokeGround(const Stroke* stroke, Point& outTopPt, Point& outBottomPt) -> bool {
    if (!stroke || stroke->getPointCount() < 4) {
        return false;
    }

    auto feat = extractFeatures(stroke);
    // Ground symbols do NOT have alternating oscillating zig-zags
    if (feat.alternatingExtremaCount >= 2 || feat.extrema.size() >= 3 || feat.bodySinuosity > 1.15) {
        return false;
    }

    auto bbox = stroke->getBoundingBox();
    if (bbox.height < 14.0 || bbox.width < 10.0) {
        return false;
    }

    // A single stroke ground is an upside-down T (starts top-center, goes down, sweeps horizontally)
    const auto& pts = stroke->getPointVector();
    const Point& p0 = pts.front();

    // Start point must be in top 35% of bounding box and roughly centered horizontally
    if (p0.y > bbox.y + bbox.height * 0.35) {
        return false;
    }
    double centerX = bbox.x + bbox.width * 0.5;
    if (std::abs(p0.x - centerX) > bbox.width * 0.35) {
        return false;
    }

    // Top stem must be vertical and roughly centered (no wide horizontal swings in top half)
    for (const auto& p: pts) {
        if (p.y < bbox.y + bbox.height * 0.50) {
            if (std::abs(p.x - centerX) > bbox.width * 0.28) {
                return false;
            }
        }
    }

    // Bottom horizontal sweep: points near the bottom edge (bottom 35%) must span horizontally
    double botMinX = 1e9;
    double botMaxX = -1e9;
    size_t botPointCount = 0;

    for (const auto& p: pts) {
        if (p.y >= bbox.y + bbox.height * 0.65) {
            botMinX = std::min(botMinX, p.x);
            botMaxX = std::max(botMaxX, p.x);
            botPointCount++;
        }
    }

    if (botPointCount >= 2 && (botMaxX - botMinX) >= bbox.width * 0.60) {
        outTopPt = Point(centerX, bbox.y);
        outBottomPt = Point(centerX, bbox.y + bbox.height);
        return true;
    }

    return false;
}

auto CircuitFeatureClassifier::classify(const Stroke* stroke) -> CircuitFeatureClass {
    if (!stroke || stroke->getPointCount() < 4) {
        return CircuitFeatureClass::Unknown;
    }

    // 0. Check Arrow
    Point shaftStart, tip;
    if (detectArrow(stroke, shaftStart, tip)) {
        return CircuitFeatureClass::Arrow;
    }

    auto feat = extractFeatures(stroke);
    auto bbox = stroke->getBoundingBox();
    double maxDim = std::max(bbox.width, bbox.height);
    double minDim = std::min(bbox.width, bbox.height);
    double aspect = (minDim > 0.0) ? (maxDim / minDim) : 100.0;

    // 1. Inductor vs Resistor discrimination with Vertex Sharpness
    // Inductors MUST have smooth, rounded U-turns (large interior vertex angles >= 110 deg).
    // Hand-drawn zig-zag resistors have sharp, acute V-reversals (<= 88 deg).
    double avgVertexAngle = CircuitDecomposer::computeAverageVertexAngle(stroke);
    bool isSharpTurns = (avgVertexAngle <= 92.0);

    // Inductor (Unipolar Coils):
    // Coils are all on one side of baseline (unipolar) with at least 2-3 coils
    if (feat.isUnipolar && feat.chordLength >= 30.0 &&
        (feat.positivePeakCount >= 3 || feat.negativeValleyCount >= 3 ||
         ((feat.positivePeakCount >= 2 || feat.negativeValleyCount >= 2) && feat.chordLength >= 40.0))) {
        return CircuitFeatureClass::Inductor;
    }

    // Resistor (IEEE Zig-Zag):
    // Works for both alternating bipolar and one-sided/tilted zig-zag strokes!
    bool hasExtrema = (feat.extrema.size() >= 3 || feat.alternatingExtremaCount >= 3);
    bool hasHighSinuosity = (feat.sinuosity >= 1.18 || (feat.hasOscillatingBody && feat.bodySinuosity >= 1.14));

    if ((maxDim >= 25.0 && aspect >= 1.35 && hasExtrema && hasHighSinuosity) || (isSharpTurns && hasExtrema && maxDim >= 22.0)) {
        return CircuitFeatureClass::ResistorIeee;
    }

    // Also support classic alternating extrema resistor:
    if (feat.chordLength >= 22.0 && feat.positivePeakCount >= 1 && feat.negativeValleyCount >= 1 &&
        (feat.positivePeakCount + feat.negativeValleyCount >= 3 || feat.alternatingExtremaCount >= 3)) {
        return CircuitFeatureClass::ResistorIeee;
    }

    // 2. Check Single-Stroke Ground (only if not an oscillating resistor/inductor)
    Point topPt, botPt;
    if (detectSingleStrokeGround(stroke, topPt, botPt)) {
        return CircuitFeatureClass::Ground;
    }

    // 3. Straight Wire:
    double maxPerp = std::max(feat.maxPerpPositive, feat.maxPerpNegative);
    if (maxPerp <= std::min(4.0, feat.chordLength * 0.04) && feat.alternatingExtremaCount <= 1 && feat.sinuosity <= 1.05) {
        return CircuitFeatureClass::StraightWire;
    }

    return CircuitFeatureClass::Unknown;
}

auto CircuitFeatureClassifier::isHandwritingOrAnnotation(const Stroke* stroke) -> bool {
    if (!stroke || stroke->getPointCount() < 2) {
        return true;
    }

    // Never classify circuit components, arrows, or grounds as handwriting
    auto featClass = classify(stroke);
    if (featClass == CircuitFeatureClass::ResistorIeee || featClass == CircuitFeatureClass::Inductor ||
        featClass == CircuitFeatureClass::SineWave || featClass == CircuitFeatureClass::Arrow ||
        featClass == CircuitFeatureClass::Ground) {
        return false;
    }

    auto bbox = stroke->getBoundingBox();
    double w = bbox.width;
    double h = bbox.height;
    double maxDim = std::max(w, h);
    double minDim = std::min(w, h);
    double diag = std::hypot(w, h);

    // 1. Tiny strokes (dots, accents, small commas, letter fragments, decimal points)
    if (diag < 18.0) {
        return true;
    }

    // 2. Check if stroke could be a circle (e.g. AC/DC source or junction node)
    double aspect = (minDim > 0.0) ? (maxDim / minDim) : 100.0;
    const auto& pts = stroke->getPointVector();
    bool isClosed = pts.front().lineLengthTo(pts.back()) <= maxDim * 0.40;
    if (aspect <= 1.35 && isClosed && maxDim >= 18.0) {
        return false;
    }

    // Protection: Elongated oscillating strokes are components (resistors, inductors, wires), NEVER handwriting!
    if (aspect >= 2.0 && maxDim >= 28.0) {
        auto feat = extractFeatures(stroke);
        if (feat.sinuosity >= 1.15 || feat.extrema.size() >= 2 || feat.alternatingExtremaCount >= 2) {
            return false;
        }
    }

    // 3. Tiny punctuation marks, dashes, minus signs, and isolated dots (length <= 12px)
    if (maxDim <= 12.0 && diag <= 14.0) {
        return true;
    }

    // 4. Compact character-sized strokes:
    // Characters ('R', 's', '5', '0', '3', '4', 'K', '1', '2', 'u', 'F', 'V', 'p', 'n', etc.)
    // have non-zero extent in both X and Y and compact dimensions (aspect ratio < 2.5).
    if (minDim >= 4.0 && maxDim <= 48.0 && aspect < 2.5) {
        auto feat = extractFeatures(stroke);
        if (feat.sinuosity > 1.12 || feat.alternatingExtremaCount >= 1 || feat.extrema.size() >= 1) {
            return true;
        }
    }

    // 5. Characters or cursive with high sinuosity:
    auto feat = extractFeatures(stroke);
    if (feat.sinuosity > 1.25 && maxDim < 50.0 && aspect < 2.5) {
        return true;
    }

    return false;
}

auto CircuitFeatureClassifier::detectBjtTransistors(const std::vector<Stroke*>& candidates) -> std::vector<BjtTransistorMatch> {
    std::vector<BjtTransistorMatch> matches;
    if (candidates.size() < 2) {
        return matches;
    }

    std::set<Stroke*> consumed;

    // Step 1: Search for candidate Base Bars
    // A base bar is a straight segment (length 14-48px) with high aspect ratio and minimal curvature
    for (Stroke* sBase: candidates) {
        if (!sBase || sBase->getPointCount() < 2 || consumed.count(sBase)) continue;

        auto box = sBase->getBoundingBox();
        double w = box.width;
        double h = box.height;
        double len = std::hypot(w, h);
        if (len < 14.0 || len > 50.0) continue;

        const auto& pts = sBase->getPointVector();
        double chordDist = pts.front().lineLengthTo(pts.back());
        if (chordDist < 12.0) continue;

        // Must be roughly vertical (height > width) or horizontal
        bool isVert = (h >= w * 1.5);
        bool isHori = (w >= h * 1.5);
        if (!isVert && !isHori) continue;

        Point baseMid((pts.front().x + pts.back().x) * 0.5, (pts.front().y + pts.back().y) * 0.5);

        // Step 2: Search for emitter and collector leads terminating near base bar
        std::vector<Stroke*> touchingLeads;
        for (Stroke* sLead: candidates) {
            if (sLead == sBase || !sLead || sLead->getPointCount() < 2 || consumed.count(sLead)) continue;

            const auto& lPts = sLead->getPointVector();
            const Point& lp0 = lPts.front();
            const Point& lp1 = lPts.back();

            double d0 = std::min(lp0.lineLengthTo(pts.front()), std::min(lp0.lineLengthTo(pts.back()), lp0.lineLengthTo(baseMid)));
            double d1 = std::min(lp1.lineLengthTo(pts.front()), std::min(lp1.lineLengthTo(pts.back()), lp1.lineLengthTo(baseMid)));

            if (d0 <= 16.0 || d1 <= 16.0) {
                touchingLeads.push_back(sLead);
            }
        }

        // BJT transistor needs at least 2 leads (emitter and collector) meeting the base bar
        if (touchingLeads.size() >= 2) {
            // Find which lead has an arrow or is slanted
            Stroke* emitter = nullptr;
            Stroke* collector = nullptr;
            bool isPnp = false;

            for (Stroke* ld: touchingLeads) {
                Point aStart, aTip;
                if (detectArrow(ld, aStart, aTip)) {
                    emitter = ld;
                    // Check if arrow points toward base bar (PNP) or away from base bar (NPN)
                    double distTipToBase = aTip.lineLengthTo(baseMid);
                    double distStartToBase = aStart.lineLengthTo(baseMid);
                    isPnp = (distTipToBase < distStartToBase);
                    break;
                }
            }

            if (!emitter) {
                emitter = touchingLeads[0];
                collector = touchingLeads[1];
            } else {
                for (Stroke* ld: touchingLeads) {
                    if (ld != emitter) {
                        collector = ld;
                        break;
                    }
                }
            }

            if (emitter && collector) {
                BjtTransistorMatch match;
                match.baseBar = sBase;
                match.emitter = emitter;
                match.collector = collector;
                match.isPnp = isPnp;
                match.isVertical = isVert;
                match.baseCenter = baseMid;

                // Base pin is offset perpendicular to the base bar
                if (isVert) {
                    // Check if leads are to the right or left of base bar
                    auto eBox = emitter->getBoundingBox();
                    auto cBox = collector->getBoundingBox();
                    double leadsCenterX = (eBox.x + cBox.x) * 0.5;
                    bool leadsOnRight = (leadsCenterX >= baseMid.x);

                    double basePinX = leadsOnRight ? (baseMid.x - 18.0) : (baseMid.x + 18.0);
                    match.basePin = Point(basePinX, baseMid.y);

                    double colY = std::min(pts.front().y, pts.back().y) - 15.0;
                    double emiY = std::max(pts.front().y, pts.back().y) + 15.0;
                    double leadEndX = leadsOnRight ? (baseMid.x + 22.0) : (baseMid.x - 22.0);

                    match.collectorPin = Point(leadEndX, colY);
                    match.emitterPin = Point(leadEndX, emiY);
                } else {
                    double leadsCenterY = (emitter->getBoundingBox().y + collector->getBoundingBox().y) * 0.5;
                    bool leadsBelow = (leadsCenterY >= baseMid.y);

                    double basePinY = leadsBelow ? (baseMid.y - 18.0) : (baseMid.y + 18.0);
                    match.basePin = Point(baseMid.x, basePinY);

                    double colX = std::min(pts.front().x, pts.back().x) - 15.0;
                    double emiX = std::max(pts.front().x, pts.back().x) + 15.0;
                    double leadEndY = leadsBelow ? (baseMid.y + 22.0) : (baseMid.y - 22.0);

                    match.collectorPin = Point(colX, leadEndY);
                    match.emitterPin = Point(emiX, leadEndY);
                }

                consumed.insert(sBase);
                consumed.insert(emitter);
                consumed.insert(collector);
                matches.push_back(match);
            }
        }
    }

    return matches;
}

auto CircuitFeatureClassifier::detectParallelCapacitors(const std::vector<Stroke*>& candidates)
        -> std::vector<ParallelCapacitorMatch> {
    std::vector<ParallelCapacitorMatch> matches;
    if (candidates.size() < 2) {
        return matches;
    }

    std::set<Stroke*> consumed;

    for (size_t i = 0; i < candidates.size(); ++i) {
        Stroke* s1 = candidates[i];
        if (!s1 || s1->getPointCount() < 2 || consumed.count(s1)) continue;

        const auto& pts1 = s1->getPointVector();
        double len1 = pts1.front().lineLengthTo(pts1.back());
        if (len1 < 12.0 || len1 > 60.0) continue;

        auto feat1 = extractFeatures(s1);
        if (feat1.sinuosity > 1.15 || feat1.alternatingExtremaCount > 1) continue;

        Point dir1((pts1.back().x - pts1.front().x) / len1, (pts1.back().y - pts1.front().y) / len1);
        Point mid1((pts1.front().x + pts1.back().x) * 0.5, (pts1.front().y + pts1.back().y) * 0.5);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            Stroke* s2 = candidates[j];
            if (!s2 || s2->getPointCount() < 2 || consumed.count(s2)) continue;

            const auto& pts2 = s2->getPointVector();
            double len2 = pts2.front().lineLengthTo(pts2.back());
            if (len2 < 12.0 || len2 > 60.0) continue;

            auto feat2 = extractFeatures(s2);
            if (feat2.sinuosity > 1.15 || feat2.alternatingExtremaCount > 1) continue;

            Point dir2((pts2.back().x - pts2.front().x) / len2, (pts2.back().y - pts2.front().y) / len2);
            Point mid2((pts2.front().x + pts2.back().x) * 0.5, (pts2.front().y + pts2.back().y) * 0.5);

            // Parallel alignment: absolute dot product between direction vectors must be close to 1
            double dot = std::abs(dir1.x * dir2.x + dir1.y * dir2.y);
            if (dot < 0.90) continue;

            // Distance between plate centers
            double centerDist = mid1.lineLengthTo(mid2);
            if (centerDist < 5.0 || centerDist > 30.0) continue;

            // Displacement vector between mid1 and mid2
            Point disp(mid2.x - mid1.x, mid2.y - mid1.y);
            double parallelOffset = std::abs(disp.x * dir1.x + disp.y * dir1.y);
            double perpDist = std::hypot(disp.x - parallelOffset * dir1.x, disp.y - parallelOffset * dir1.y);

            // Perpendicular gap must dominate; parallel offset between plates must be small
            if (perpDist < 5.0 || perpDist > 25.0 || parallelOffset > std::max(len1, len2) * 0.55) {
                continue;
            }

            // Both plates are roughly vertical (dir.y dominates) -> connection is horizontal
            // Or plates are roughly horizontal (dir.x dominates) -> connection is vertical
            bool isPlatesVertical = (std::abs(dir1.y) >= std::abs(dir1.x));

            ParallelCapacitorMatch match;
            match.plate1 = s1;
            match.plate2 = s2;
            match.plate1Center = mid1;
            match.plate2Center = mid2;
            match.plateLength = std::max(len1, len2);
            match.plateGap = perpDist;
            match.center = Point((mid1.x + mid2.x) * 0.5, (mid1.y + mid2.y) * 0.5);
            match.isHorizontal = isPlatesVertical;

            constexpr double leadLen = 14.0;
            if (match.isHorizontal) {
                // Plates are vertical, leads extend horizontally outward to the left and right
                Point leftCenter = (mid1.x < mid2.x) ? mid1 : mid2;
                Point rightCenter = (mid1.x < mid2.x) ? mid2 : mid1;

                match.terminalA = Point(leftCenter.x - leadLen, match.center.y);
                match.terminalB = Point(rightCenter.x + leadLen, match.center.y);
            } else {
                // Plates are horizontal, leads extend vertically outward top and bottom
                Point topCenter = (mid1.y < mid2.y) ? mid1 : mid2;
                Point bottomCenter = (mid1.y < mid2.y) ? mid2 : mid1;

                match.terminalA = Point(match.center.x, topCenter.y - leadLen);
                match.terminalB = Point(match.center.x, bottomCenter.y + leadLen);
            }

            consumed.insert(s1);
            consumed.insert(s2);
            matches.push_back(match);
            break;
        }
    }

    return matches;
}

auto CircuitFeatureClassifier::detectNodeMarkers(
        const std::vector<Stroke*>& candidates,
        const std::set<Stroke*>& textClusterStrokes) -> std::vector<CircuitNodeMarker> {
    std::vector<CircuitNodeMarker> markers;
    if (candidates.empty()) {
        return markers;
    }

    // 1. Gather all non-text strokes to verify circuit proximity
    std::vector<Stroke*> circuitStrokes;
    for (Stroke* s: candidates) {
        if (!s || textClusterStrokes.count(s)) continue;
        circuitStrokes.push_back(s);
    }

    // 2. Identify candidate marker strokes
    for (Stroke* s: circuitStrokes) {
        if (!s || s->getPointCount() < 1) continue;

        auto bbox = s->getBoundingBox();
        double maxDim = std::max(bbox.width, bbox.height);
        double minDim = std::min(bbox.width, bbox.height);
        double diag = std::hypot(bbox.width, bbox.height);

        // Marker strokes are compact (small dots or small circular terminal loops)
        // Terminal loops are typically 7px - 36px in diameter
        // Solder dots are typically 2px - 25px in diameter
        if (diag > 42.0 || maxDim > 38.0) {
            continue;
        }

        const auto& pts = s->getPointVector();

        // Compute centroid
        Point center(0.0, 0.0);
        for (const auto& p: pts) {
            center.x += p.x;
            center.y += p.y;
        }
        center.x /= static_cast<double>(pts.size());
        center.y /= static_cast<double>(pts.size());

        // 3. Proximity check: Must be near other circuit elements (wire endpoints or lines)
        bool nearCircuit = false;
        constexpr double MAX_CIRCUIT_PROXIMITY_SQ = 28.0 * 28.0;

        for (Stroke* other: circuitStrokes) {
            if (other == s || other->getPointCount() < 2) continue;
            auto otherBox = other->getBoundingBox();
            double otherMaxDim = std::max(otherBox.width, otherBox.height);
            // Skip other tiny dots
            if (otherMaxDim <= 25.0 && std::hypot(otherBox.width, otherBox.height) <= 28.0) {
                continue;
            }

            const auto& oPts = other->getPointVector();
            // Check distance to endpoints
            double dStartSq = (center.x - oPts.front().x) * (center.x - oPts.front().x) +
                              (center.y - oPts.front().y) * (center.y - oPts.front().y);
            double dEndSq = (center.x - oPts.back().x) * (center.x - oPts.back().x) +
                            (center.y - oPts.back().y) * (center.y - oPts.back().y);

            if (dStartSq <= MAX_CIRCUIT_PROXIMITY_SQ || dEndSq <= MAX_CIRCUIT_PROXIMITY_SQ) {
                nearCircuit = true;
                break;
            }

            // Check distance to line segments of other
            for (size_t i = 0; i + 1 < oPts.size(); ++i) {
                Point p1 = oPts[i];
                Point p2 = oPts[i + 1];
                double segDx = p2.x - p1.x;
                double segDy = p2.y - p1.y;
                double segLenSq = segDx * segDx + segDy * segDy;
                if (segLenSq < 1e-4) continue;
                double t = std::clamp(((center.x - p1.x) * segDx + (center.y - p1.y) * segDy) / segLenSq, 0.0, 1.0);
                Point proj(p1.x + t * segDx, p1.y + t * segDy);
                double dSq = (center.x - proj.x) * (center.x - proj.x) + (center.y - proj.y) * (center.y - proj.y);
                if (dSq <= 12.0 * 12.0) {
                    nearCircuit = true;
                    break;
                }
            }
            if (nearCircuit) break;
        }

        if (!nearCircuit && circuitStrokes.size() > 1) {
            // Stray dot far from any circuit element
            continue;
        }

        // 4. Classify OPEN_TERMINAL vs SOLDER_JUNCTION
        double endGap = pts.front().lineLengthTo(pts.back());
        bool isClosed = (pts.size() >= 4 && endGap <= maxDim * 0.55);
        double aspect = (minDim > 0.0) ? (maxDim / minDim) : 1.0;

        double meanR = 0.0;
        for (const auto& p: pts) {
            meanR += p.lineLengthTo(center);
        }
        meanR /= static_cast<double>(pts.size());

        double varR = 0.0;
        for (const auto& p: pts) {
            double diff = p.lineLengthTo(center) - meanR;
            varR += diff * diff;
        }
        varR /= static_cast<double>(pts.size());
        double stdR = std::sqrt(varR);

        CircuitNodeMarker marker;
        marker.originalStroke = s;
        marker.center = center;

        // Open terminal: a circular loop (aspect close to 1, hollow ring with low radial variance, diameter >= 6px)
        if (isClosed && aspect <= 1.65 && maxDim >= 6.0 && meanR >= 2.5 && (stdR / meanR < 0.45)) {
            marker.type = CircuitNodeMarkerType::OPEN_TERMINAL;
            marker.radius = std::clamp(meanR, 3.5, 6.5);
        } else {
            marker.type = CircuitNodeMarkerType::SOLDER_JUNCTION;
            marker.radius = 3.5;
        }

        markers.push_back(marker);
    }

    // 5. Collinear Dot Alignment (align dots that share similar X or Y coordinates)
    // Vertical alignment (align X)
    for (size_t i = 0; i < markers.size(); ++i) {
        std::vector<size_t> group = {i};
        for (size_t j = i + 1; j < markers.size(); ++j) {
            if (std::abs(markers[i].center.x - markers[j].center.x) <= 8.0) {
                group.push_back(j);
            }
        }
        if (group.size() >= 2) {
            double sumX = 0.0;
            for (size_t idx: group) {
                sumX += markers[idx].center.x;
            }
            double avgX = sumX / static_cast<double>(group.size());
            for (size_t idx: group) {
                markers[idx].center.x = avgX;
                markers[idx].isCollinearAligned = true;
            }
        }
    }

    // Horizontal alignment (align Y)
    for (size_t i = 0; i < markers.size(); ++i) {
        std::vector<size_t> group = {i};
        for (size_t j = i + 1; j < markers.size(); ++j) {
            if (std::abs(markers[i].center.y - markers[j].center.y) <= 8.0) {
                group.push_back(j);
            }
        }
        if (group.size() >= 2) {
            double sumY = 0.0;
            for (size_t idx: group) {
                sumY += markers[idx].center.y;
            }
            double avgY = sumY / static_cast<double>(group.size());
            for (size_t idx: group) {
                markers[idx].center.y = avgY;
                markers[idx].isCollinearAligned = true;
            }
        }
    }

    return markers;
}

}  // namespace xoj::circuit
