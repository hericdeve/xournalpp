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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "control/shaperecognizer/svg/SvgPathParser.h"
#include "model/Point.h"

namespace xoj::circuit {

class CircuitTemplate {
public:
    CircuitTemplate() = default;
    CircuitTemplate(std::string id, std::string displayName, xoj::svg::SvgDocument doc);

    static auto fromSvgString(const std::string& id, const std::string& displayName, const std::string& svgContent)
            -> std::shared_ptr<CircuitTemplate>;
    static auto fromSvgFile(const std::string& filePath) -> std::shared_ptr<CircuitTemplate>;

    [[nodiscard]] auto getId() const -> const std::string& { return id; }
    [[nodiscard]] auto getDisplayName() const -> const std::string& { return displayName; }
    [[nodiscard]] auto isEnabled() const -> bool { return enabled; }
    void setEnabled(bool en) { enabled = en; }

    [[nodiscard]] auto getSubpaths() const -> const std::vector<xoj::svg::SvgSubpath>& { return subpaths; }
    [[nodiscard]] auto getCombinedPath() const -> const std::vector<Point>& { return combinedPath; }

    [[nodiscard]] auto getTerminalA() const -> const Point& { return terminalA; }
    [[nodiscard]] auto getTerminalB() const -> const Point& { return terminalB; }
    [[nodiscard]] auto getNominalLength() const -> double { return nominalLength; }
    [[nodiscard]] auto getBodyStartOffset() const -> double { return bodyStartOffset; }
    [[nodiscard]] auto getBodyEndOffset() const -> double { return bodyEndOffset; }
    [[nodiscard]] auto getBodyWidth() const -> double { return bodyWidth; }

    [[nodiscard]] auto getNormalizedCloud() const -> const std::vector<Point>& { return normalizedCloud; }
    [[nodiscard]] auto getSinuosity() const -> double { return sinuosity; }

    static auto resampleEquidistant(const std::vector<Point>& points, size_t n = 48) -> std::vector<Point>;

private:
    void initFromDocument(const xoj::svg::SvgDocument& doc);

    std::string id;
    std::string displayName;
    bool enabled{true};

    std::vector<xoj::svg::SvgSubpath> subpaths;
    std::vector<Point> combinedPath;

    Point terminalA{0.0, 0.0};
    Point terminalB{100.0, 0.0};
    double nominalLength{100.0};
    double bodyStartOffset{20.0};
    double bodyEndOffset{80.0};
    double bodyWidth{60.0};

    std::vector<Point> normalizedCloud;
    double sinuosity{1.0};
};

}  // namespace xoj::circuit
