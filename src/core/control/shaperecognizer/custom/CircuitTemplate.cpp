/*
 * Xournal++
 *
 * Circuit Component Template for Shape Recognition
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "CircuitTemplate.h"

#include <algorithm>
#include <cmath>

#include "util/PathUtil.h"

namespace xoj::circuit {

CircuitTemplate::CircuitTemplate(std::string id, std::string displayName, xoj::svg::SvgDocument doc):
        id(std::move(id)), displayName(std::move(displayName)) {
    initFromDocument(doc);
}

auto CircuitTemplate::fromSvgString(const std::string& id, const std::string& displayName,
                                    const std::string& svgContent) -> std::shared_ptr<CircuitTemplate> {
    auto doc = xoj::svg::SvgPathParser::parseSvgString(svgContent);
    if (doc.empty()) {
        return nullptr;
    }
    return std::make_shared<CircuitTemplate>(id, displayName, std::move(doc));
}

auto CircuitTemplate::fromSvgFile(const std::string& filePath) -> std::shared_ptr<CircuitTemplate> {
    auto doc = xoj::svg::SvgPathParser::parseSvgFile(filePath);
    if (doc.empty()) {
        return nullptr;
    }
    fs::path p(filePath);
    std::string stem = p.stem().string();
    return std::make_shared<CircuitTemplate>(stem, stem, std::move(doc));
}

auto CircuitTemplate::resampleEquidistant(const std::vector<Point>& points, size_t n) -> std::vector<Point> {
    std::vector<Point> res;
    if (points.empty() || n == 0) {
        return res;
    }
    if (points.size() == 1 || n == 1) {
        res.assign(n, points.front());
        return res;
    }

    // Compute segment lengths and total length
    std::vector<double> segLens;
    segLens.reserve(points.size() - 1);
    double totalLen = 0.0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        double d = points[i].lineLengthTo(points[i + 1]);
        segLens.push_back(d);
        totalLen += d;
    }

    if (totalLen < 1e-6) {
        res.assign(n, points.front());
        return res;
    }

    double interval = totalLen / static_cast<double>(n - 1);
    res.push_back(points.front());

    size_t segIdx = 0;
    double currentSegDist = 0.0;

    for (size_t i = 1; i + 1 < n; ++i) {
        double targetDist = static_cast<double>(i) * interval;
        while (segIdx < segLens.size() && currentSegDist + segLens[segIdx] < targetDist) {
            currentSegDist += segLens[segIdx];
            segIdx++;
        }

        if (segIdx >= segLens.size()) {
            res.push_back(points.back());
        } else {
            double remain = targetDist - currentSegDist;
            double t = (segLens[segIdx] > 1e-6) ? (remain / segLens[segIdx]) : 0.0;
            const Point& p0 = points[segIdx];
            const Point& p1 = points[segIdx + 1];
            res.emplace_back(p0.x + t * (p1.x - p0.x), p0.y + t * (p1.y - p0.y));
        }
    }

    res.push_back(points.back());
    return res;
}

void CircuitTemplate::initFromDocument(const xoj::svg::SvgDocument& doc) {
    this->subpaths = doc.subpaths;
    this->combinedPath = doc.getCombinedPoints();

    if (this->combinedPath.empty()) {
        return;
    }

    // Terminal A: start of first subpath
    this->terminalA = this->combinedPath.front();
    // Terminal B: end of last subpath
    this->terminalB = this->combinedPath.back();

    this->nominalLength = this->terminalA.lineLengthTo(this->terminalB);
    if (this->nominalLength < 1.0) {
        this->nominalLength = 1.0;
    }

    // Baseline direction
    double dx = this->terminalB.x - this->terminalA.x;
    double dy = this->terminalB.y - this->terminalA.y;
    double angle = std::atan2(dy, dx);
    double cosA = std::cos(-angle);
    double sinA = std::sin(-angle);

    // Compute projections along the baseline to auto-detect bodyStart and bodyEnd
    double minU = this->nominalLength;
    double maxU = 0.0;
    bool foundDeviation = false;

    double totalArcLen = 0.0;
    for (size_t i = 0; i + 1 < this->combinedPath.size(); ++i) {
        totalArcLen += this->combinedPath[i].lineLengthTo(this->combinedPath[i + 1]);
    }
    this->sinuosity = totalArcLen / this->nominalLength;

    for (const auto& pt: this->combinedPath) {
        double px = pt.x - this->terminalA.x;
        double py = pt.y - this->terminalA.y;
        double u = cosA * px - sinA * py;
        double v = sinA * px + cosA * py;

        if (std::abs(v) > 0.03 * this->nominalLength) {
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            foundDeviation = true;
        }
    }

    if (foundDeviation && minU < maxU) {
        this->bodyStartOffset = std::clamp(minU, 0.0, this->nominalLength * 0.45);
        this->bodyEndOffset = std::clamp(maxU, this->nominalLength * 0.55, this->nominalLength);
    } else {
        this->bodyStartOffset = this->nominalLength * 0.20;
        this->bodyEndOffset = this->nominalLength * 0.80;
    }
    this->bodyWidth = this->bodyEndOffset - this->bodyStartOffset;

    // Build normalized point cloud: resampled to 48 points, translated/rotated/scaled
    auto resampled = resampleEquidistant(this->combinedPath, 48);
    this->normalizedCloud.clear();
    this->normalizedCloud.reserve(resampled.size());

    for (const auto& pt: resampled) {
        double px = pt.x - this->terminalA.x;
        double py = pt.y - this->terminalA.y;
        double u = (cosA * px - sinA * py) / this->nominalLength;
        double v = (sinA * px + cosA * py) / this->nominalLength;
        this->normalizedCloud.emplace_back(u, v);
    }
}

}  // namespace xoj::circuit
