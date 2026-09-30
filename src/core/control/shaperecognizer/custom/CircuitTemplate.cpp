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
#include "util/Rectangle.h"

namespace xoj::circuit {

using xoj::util::Rectangle;

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

    // Compute bounding box and centroid
    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    double sumX = 0.0;
    double sumY = 0.0;

    for (const auto& pt: this->combinedPath) {
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
        sumX += pt.x;
        sumY += pt.y;
    }

    this->templateBbox = Rectangle<double>(minX, minY, std::max(maxX - minX, 1.0), std::max(maxY - minY, 1.0));
    this->centroid = Point(sumX / static_cast<double>(this->combinedPath.size()),
                           sumY / static_cast<double>(this->combinedPath.size()));
    this->templateHeight = this->templateBbox.height;

    // Terminal A: start of first subpath
    this->terminalA = this->combinedPath.front();
    // Terminal B: end of last subpath
    this->terminalB = this->combinedPath.back();

    double termDist = this->terminalA.lineLengthTo(this->terminalB);
    double diag = std::hypot(this->templateBbox.width, this->templateBbox.height);

    if (termDist < 0.20 * diag || (this->subpaths.size() == 1 && this->subpaths.front().closed)) {
        this->kind = TemplateKind::ClosedShape;
        this->nominalLength = diag;
    } else {
        this->kind = TemplateKind::TwoTerminal;
        this->nominalLength = std::max(termDist, 1.0);
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

    if (this->kind == TemplateKind::ClosedShape) {
        // Closed shape normalized cloud: relative to centroid and diagonal size
        for (const auto& pt: resampled) {
            double nx = (pt.x - this->centroid.x) / diag;
            double ny = (pt.y - this->centroid.y) / diag;
            this->normalizedCloud.emplace_back(nx, ny);
        }
    } else {
        // Two-terminal cloud: relative to terminal axis
        for (const auto& pt: resampled) {
            double px = pt.x - this->terminalA.x;
            double py = pt.y - this->terminalA.y;
            double u = (cosA * px - sinA * py) / this->nominalLength;
            double v = (sinA * px + cosA * py) / this->nominalLength;
            this->normalizedCloud.emplace_back(u, v);
        }
    }

    // Build normalized body cloud (for scale-independent and lead-independent two-terminal matching)
    double maxDev = 0.0;
    std::vector<Point> rawBodyPts;
    for (const auto& pt: this->combinedPath) {
        double px = pt.x - this->terminalA.x;
        double py = pt.y - this->terminalA.y;
        double u = cosA * px - sinA * py;
        double v = sinA * px + cosA * py;
        if (u >= this->bodyStartOffset - 1e-4 && u <= this->bodyEndOffset + 1e-4) {
            rawBodyPts.emplace_back(u, v);
            maxDev = std::max(maxDev, std::abs(v));
        }
    }
    if (rawBodyPts.size() < 2) {
        rawBodyPts.emplace_back(this->bodyStartOffset, 0.0);
        rawBodyPts.emplace_back(this->bodyEndOffset, 0.0);
    }
    this->maxBodyDeviation = (maxDev > 1e-4) ? maxDev : 12.0;

    auto resampledBody = resampleEquidistant(rawBodyPts, 32);
    this->bodyCloud.clear();
    this->bodyCloud.reserve(resampledBody.size());
    for (const auto& pt: resampledBody) {
        double unorm = (this->bodyWidth > 1e-4) ? ((pt.x - this->bodyStartOffset) / this->bodyWidth) : 0.0;
        double vnorm = (this->maxBodyDeviation > 1e-4) ? (pt.y / this->maxBodyDeviation) : 0.0;
        this->bodyCloud.emplace_back(unorm, vnorm);
    }
}

}  // namespace xoj::circuit
