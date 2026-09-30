/*
 * Xournal++
 *
 * SVG Path and Vector Parser for Shape Recognition
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <vector>

#include "model/Point.h"

namespace xoj::svg {

struct SvgSubpath {
    std::vector<Point> points;
    bool closed = false;
};

struct SvgDocument {
    double width = 0.0;
    double height = 0.0;
    double viewBoxX = 0.0;
    double viewBoxY = 0.0;
    double viewBoxWidth = 0.0;
    double viewBoxHeight = 0.0;
    std::vector<SvgSubpath> subpaths;

    [[nodiscard]] bool empty() const {
        for (const auto& sp: subpaths) {
            if (!sp.points.empty()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<Point> getCombinedPoints() const {
        std::vector<Point> result;
        for (const auto& sp: subpaths) {
            result.insert(result.end(), sp.points.begin(), sp.points.end());
        }
        return result;
    }
};

class SvgPathParser {
public:
    static auto parseSvgString(const std::string& svgContent) -> SvgDocument;
    static auto parseSvgFile(const std::string& filePath) -> SvgDocument;
    static auto parsePathData(const std::string& d) -> std::vector<SvgSubpath>;
};

}  // namespace xoj::svg
