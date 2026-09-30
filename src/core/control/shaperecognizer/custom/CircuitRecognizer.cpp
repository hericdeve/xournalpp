/*
 * Xournal++
 *
 * Circuit Component Pattern Recognizer
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "CircuitRecognizer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "CircuitFeatureClassifier.h"
#include "util/Rectangle.h"

namespace xoj::circuit {

using xoj::util::Rectangle;

auto CircuitRecognizer::recognize(const Stroke* stroke,
                                  const std::vector<std::shared_ptr<CircuitTemplate>>& templates, double threshold)
        -> CircuitRecognitionResult {
    CircuitRecognitionResult result;
    if (!stroke || stroke->getPointCount() < 6 || templates.empty()) {
        return result;
    }

    const auto& pts = stroke->getPointVector();
    const Point& startPt = pts.front();
    const Point& endPt = pts.back();

    double terminalDist = startPt.lineLengthTo(endPt);
    Rectangle<double> bbox = stroke->getBoundingBox();
    double bboxDiag = std::hypot(bbox.width, bbox.height);

    if (bboxDiag < 10.0) {
        return result;
    }

    // Direct structural feature classification (bypasses point-cloud ambiguity for oscillating components)
    auto featClass = CircuitFeatureClassifier::classify(stroke);
    auto feat = CircuitFeatureClassifier::extractFeatures(stroke);

    if (featClass == CircuitFeatureClass::ResistorIeee) {
        for (const auto& tpl: templates) {
            if (tpl && tpl->isEnabled() && (tpl->getId() == "resistor_ieee" || tpl->getId() == "resistor_iec")) {
                result.matched = true;
                result.matchedTemplate = tpl.get();
                result.score = 0.95;
                result.reversed = false;
                result.terminalStart = startPt;
                result.terminalEnd = endPt;
                if (feat.hasOscillatingBody && feat.bodyEndU > feat.bodyStartU + 0.10) {
                    result.bodyStartRatio = feat.bodyStartU;
                    result.bodyEndRatio = feat.bodyEndU;
                }
                return result;
            }
        }
    } else if (featClass == CircuitFeatureClass::Inductor) {
        for (const auto& tpl: templates) {
            if (tpl && tpl->isEnabled() && tpl->getId() == "inductor") {
                result.matched = true;
                result.matchedTemplate = tpl.get();
                result.score = 0.95;
                result.reversed = false;
                result.terminalStart = startPt;
                result.terminalEnd = endPt;
                if (feat.hasOscillatingBody && feat.bodyEndU > feat.bodyStartU + 0.10) {
                    result.bodyStartRatio = feat.bodyStartU;
                    result.bodyEndRatio = feat.bodyEndU;
                }
                return result;
            }
        }
    } else if (featClass == CircuitFeatureClass::Ground) {
        for (const auto& tpl: templates) {
            if (tpl && tpl->isEnabled() && tpl->getId() == "ground") {
                Point topPt, botPt;
                CircuitFeatureClassifier::detectSingleStrokeGround(stroke, topPt, botPt);
                result.matched = true;
                result.matchedTemplate = tpl.get();
                result.score = 0.95;
                result.reversed = false;
                result.terminalStart = topPt;
                result.terminalEnd = botPt;
                return result;
            }
        }
    } else if (featClass == CircuitFeatureClass::StraightWire) {
        // Definitely a wire, do not match any complex circuit template
        return result;
    }

    double totalArcLen = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        totalArcLen += pts[i].lineLengthTo(pts[i + 1]);
    }
    double strokeSinuosity = (terminalDist > 1.0) ? (totalArcLen / terminalDist) : 5.0;

    // Resample candidate to 48 points for full-stroke point cloud
    auto resampled = CircuitTemplate::resampleEquidistant(pts, 48);
    const size_t N = resampled.size();
    if (N != 48) {
        return result;
    }

    // Baseline direction
    double cosA = 1.0;
    double sinA = 0.0;
    bool validTwoTerm = (terminalDist >= 10.0);
    if (validTwoTerm) {
        double dx = endPt.x - startPt.x;
        double dy = endPt.y - startPt.y;
        double angle = std::atan2(dy, dx);
        cosA = std::cos(-angle);
        sinA = std::sin(-angle);
    }

    // 1. Candidate cloud for two-terminal components (aligned to start -> end baseline)
    std::vector<Point> candidateTwoTerm;
    if (validTwoTerm) {
        candidateTwoTerm.reserve(N);
        for (const auto& p: resampled) {
            double px = p.x - startPt.x;
            double py = p.y - startPt.y;
            double u = (cosA * px - sinA * py) / terminalDist;
            double v = (sinA * px + cosA * py) / terminalDist;
            candidateTwoTerm.emplace_back(u, v);
        }
    }

    // 2. Candidate BODY cloud (scale-independent and lead-length independent)
    // Find perpendicular deviations from baseline across all points
    double maxPerpDev = 0.0;
    std::vector<double> uVals;
    std::vector<double> vVals;
    uVals.reserve(pts.size());
    vVals.reserve(pts.size());

    if (validTwoTerm) {
        for (const auto& p: pts) {
            double px = p.x - startPt.x;
            double py = p.y - startPt.y;
            double u = cosA * px - sinA * py;
            double v = sinA * px + cosA * py;
            uVals.push_back(u);
            vVals.push_back(v);
            maxPerpDev = std::max(maxPerpDev, std::abs(v));
        }
    }

    std::vector<Point> candidateBodyCloud;
    bool hasCandidateBody = false;

    if (validTwoTerm && maxPerpDev >= 2.0 && pts.size() >= 8) {
        size_t idxStart = 0;
        size_t idxEnd = pts.size() - 1;
        double devCutoff = std::max(1.2, maxPerpDev * 0.15);

        while (idxStart < pts.size() && std::abs(vVals[idxStart]) < devCutoff) {
            idxStart++;
        }
        while (idxEnd > idxStart && std::abs(vVals[idxEnd]) < devCutoff) {
            idxEnd--;
        }

        if (idxStart > 0) {
            idxStart--;
        }
        if (idxEnd + 1 < pts.size()) {
            idxEnd++;
        }

        if (idxEnd > idxStart + 4) {
            std::vector<Point> bodyPts;
            for (size_t i = idxStart; i <= idxEnd; ++i) {
                bodyPts.push_back(pts[i]);
            }
            auto resampledBody = CircuitTemplate::resampleEquidistant(bodyPts, 32);
            if (resampledBody.size() == 32) {
                double uBodyStart = uVals[idxStart];
                double uBodyEnd = uVals[idxEnd];
                double uBodySpan = std::max(uBodyEnd - uBodyStart, 1.0);
                candidateBodyCloud.reserve(32);
                for (const auto& p: resampledBody) {
                    double px = p.x - startPt.x;
                    double py = p.y - startPt.y;
                    double u = cosA * px - sinA * py;
                    double v = sinA * px + cosA * py;
                    double unorm = (u - uBodyStart) / uBodySpan;
                    double vnorm = v / maxPerpDev;
                    candidateBodyCloud.emplace_back(unorm, vnorm);
                }
                hasCandidateBody = true;
            }
        }
    }

    // 3. Candidate cloud for closed shapes (relative to centroid and bounding diagonal)
    double sumX = 0.0;
    double sumY = 0.0;
    for (const auto& p: resampled) {
        sumX += p.x;
        sumY += p.y;
    }
    Point candidateCentroid(sumX / static_cast<double>(N), sumY / static_cast<double>(N));
    std::vector<Point> candidateClosed;
    candidateClosed.reserve(N);
    for (const auto& p: resampled) {
        candidateClosed.emplace_back((p.x - candidateCentroid.x) / bboxDiag,
                                     (p.y - candidateCentroid.y) / bboxDiag);
    }

    double bestScore = 0.0;
    const CircuitTemplate* bestTemplate = nullptr;
    bool bestReversed = false;

    for (const auto& tpl: templates) {
        if (!tpl || !tpl->isEnabled()) {
            continue;
        }

        if (tpl->getKind() == TemplateKind::ClosedShape) {
            // Closed shape comparison: test rotational/cyclic shifts around the loop
            const auto& tplCloud = tpl->getNormalizedCloud();
            if (tplCloud.size() != N) {
                continue;
            }

            double bestClosedDist = std::numeric_limits<double>::infinity();
            for (size_t shift = 0; shift < N; shift += 4) {
                double dFwd = 0.0;
                double dRev = 0.0;
                for (size_t i = 0; i < N; ++i) {
                    const Point& c = candidateClosed[(i + shift) % N];
                    const Point& cRev = candidateClosed[(N + shift - i) % N];
                    const Point& t = tplCloud[i];
                    dFwd += std::hypot(c.x - t.x, c.y - t.y);
                    dRev += std::hypot(cRev.x - t.x, cRev.y - t.y);
                }
                bestClosedDist = std::min({bestClosedDist, dFwd / static_cast<double>(N), dRev / static_cast<double>(N)});
            }

            double score = std::max(0.0, 1.0 - (bestClosedDist / 0.35));
            if (score > bestScore) {
                bestScore = score;
                bestTemplate = tpl.get();
                bestReversed = false;
            }
        } else {
            // Two-terminal or directional component comparison
            if (!validTwoTerm) {
                continue;
            }

            // Sinuosity check for oscillating components (resistor, inductor)
            double effectiveSinuosity = feat.hasOscillatingBody ? std::max(strokeSinuosity, feat.bodySinuosity) : strokeSinuosity;
            if (tpl->getSinuosity() > 1.30 && effectiveSinuosity < 1.10 && maxPerpDev < 3.0) {
                continue;
            }

            // A stroke with multiple alternating extrema or significant sinuosity can NEVER be a capacitor
            if (tpl->getId() == "capacitor" && (feat.alternatingExtremaCount >= 2 || strokeSinuosity > 1.25 || feat.positivePeakCount >= 2)) {
                continue;
            }

            // A stroke with bipolar oscillations can NEVER be an inductor
            if (tpl->getId() == "inductor" && (feat.positivePeakCount >= 1 && feat.negativeValleyCount >= 1)) {
                continue;
            }

            double score = 0.0;
            bool isReversed = false;

            // Strategy A: Scale-independent & lead-independent BODY matching
            const auto& tplBody = tpl->getBodyCloud();
            if (hasCandidateBody && tplBody.size() == 32) {
                double sumBodyFwd = 0.0;
                double sumBodyRev = 0.0;
                double sumBodyFwdFlip = 0.0;
                double sumBodyRevFlip = 0.0;

                for (size_t i = 0; i < 32; ++i) {
                    const Point& c = candidateBodyCloud[i];
                    const Point& cRev = candidateBodyCloud[31 - i];
                    const Point& t = tplBody[i];

                    sumBodyFwd += std::hypot(c.x - t.x, c.y - t.y);
                    sumBodyRev += std::hypot(cRev.x - t.x, cRev.y - t.y);
                    sumBodyFwdFlip += std::hypot(c.x - t.x, -c.y - t.y);
                    sumBodyRevFlip += std::hypot(cRev.x - t.x, -cRev.y - t.y);
                }

                double minBodyFwd = std::min(sumBodyFwd, sumBodyFwdFlip) / 32.0;
                double minBodyRev = std::min(sumBodyRev, sumBodyRevFlip) / 32.0;
                double bestBodyDist = std::min(minBodyFwd, minBodyRev);
                bool bodyRev = (minBodyRev < minBodyFwd);

                double bodyScore = std::max(0.0, 1.0 - (bestBodyDist / 0.50));
                score = std::max(score, bodyScore);
                isReversed = bodyRev;
            }

            // Strategy B: Full-stroke baseline cloud matching (for templates with exact lead ratios or ground)
            const auto& tplCloud = tpl->getNormalizedCloud();
            if (tplCloud.size() == N && candidateTwoTerm.size() == N) {
                double sumFwd = 0.0;
                double sumRev = 0.0;
                double sumFwdFlip = 0.0;
                double sumRevFlip = 0.0;

                for (size_t i = 0; i < N; ++i) {
                    const Point& c = candidateTwoTerm[i];
                    const Point& cRev = candidateTwoTerm[N - 1 - i];
                    const Point& t = tplCloud[i];

                    sumFwd += std::hypot(c.x - t.x, c.y - t.y);
                    sumRev += std::hypot(cRev.x - t.x, cRev.y - t.y);
                    sumFwdFlip += std::hypot(c.x - t.x, -c.y - t.y);
                    sumRevFlip += std::hypot(cRev.x - t.x, -cRev.y - t.y);
                }

                double minFwd = std::min(sumFwd, sumFwdFlip) / static_cast<double>(N);
                double minRev = std::min(sumRev, sumRevFlip) / static_cast<double>(N);
                double bestFullDist = std::min(minFwd, minRev);
                double fullScore = std::max(0.0, 1.0 - (bestFullDist / 0.38));

                if (fullScore > score) {
                    score = fullScore;
                    isReversed = (minRev < minFwd);
                }
            }

            if (score > bestScore) {
                bestScore = score;
                bestTemplate = tpl.get();
                bestReversed = isReversed;
            }
        }
    }

    if (bestTemplate && bestScore >= threshold) {
        result.matched = true;
        result.matchedTemplate = bestTemplate;
        result.score = bestScore;
        result.reversed = bestReversed;
        if (bestReversed) {
            result.terminalStart = endPt;
            result.terminalEnd = startPt;
        } else {
            result.terminalStart = startPt;
            result.terminalEnd = endPt;
        }
    }

    return result;
}

}  // namespace xoj::circuit
