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

namespace xoj::circuit {

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
    if (terminalDist < 12.0) {
        return result;
    }

    double totalArcLen = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        totalArcLen += pts[i].lineLengthTo(pts[i + 1]);
    }
    double sinuosity = totalArcLen / terminalDist;
    if (sinuosity < 1.10) {
        // Trivial straight line, let primitive shape recognizer handle it
        return result;
    }

    // Baseline direction
    double dx = endPt.x - startPt.x;
    double dy = endPt.y - startPt.y;
    double angle = std::atan2(dy, dx);
    double cosA = std::cos(-angle);
    double sinA = std::sin(-angle);

    // Resample candidate to 48 points
    auto resampled = CircuitTemplate::resampleEquidistant(pts, 48);
    std::vector<Point> candidateCloud;
    candidateCloud.reserve(resampled.size());

    for (const auto& p: resampled) {
        double px = p.x - startPt.x;
        double py = p.y - startPt.y;
        double u = (cosA * px - sinA * py) / terminalDist;
        double v = (sinA * px + cosA * py) / terminalDist;
        candidateCloud.emplace_back(u, v);
    }

    const size_t N = candidateCloud.size();
    if (N != 48) {
        return result;
    }

    double bestScore = 0.0;
    const CircuitTemplate* bestTemplate = nullptr;
    bool bestReversed = false;

    for (const auto& tpl: templates) {
        if (!tpl || !tpl->isEnabled()) {
            continue;
        }

        const auto& tplCloud = tpl->getNormalizedCloud();
        if (tplCloud.size() != N) {
            continue;
        }

        double sumFwd = 0.0;
        double sumRev = 0.0;
        double sumFwdFlip = 0.0;
        double sumRevFlip = 0.0;

        for (size_t i = 0; i < N; ++i) {
            const Point& c = candidateCloud[i];
            const Point& cRev = candidateCloud[N - 1 - i];
            const Point& t = tplCloud[i];

            sumFwd += std::hypot(c.x - t.x, c.y - t.y);
            sumRev += std::hypot(cRev.x - t.x, cRev.y - t.y);
            sumFwdFlip += std::hypot(c.x - t.x, -c.y - t.y);
            sumRevFlip += std::hypot(cRev.x - t.x, -cRev.y - t.y);
        }

        double minFwd = std::min(sumFwd, sumFwdFlip) / static_cast<double>(N);
        double minRev = std::min(sumRev, sumRevFlip) / static_cast<double>(N);

        bool isReversed = (minRev < minFwd);
        double bestDist = std::min(minFwd, minRev);

        // Score based on distance tolerance ~0.35 in normalized coords
        double score = std::max(0.0, 1.0 - (bestDist / 0.35));

        // Sinuosity compatibility check:
        // A resistor or inductor has sinuosity ~1.6 - 2.8.
        if (tpl->getSinuosity() > 1.35 && sinuosity < 1.18) {
            score *= 0.6;
        }

        if (score > bestScore) {
            bestScore = score;
            bestTemplate = tpl.get();
            bestReversed = isReversed;
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
