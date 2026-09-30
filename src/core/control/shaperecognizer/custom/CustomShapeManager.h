/*
 * Xournal++
 *
 * Custom Shape & Circuit Template Manager
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

#include "CircuitRecognizer.h"
#include "CircuitSnapper.h"
#include "CircuitTemplate.h"
#include "filesystem.h"

class Stroke;
class SnapToGridInputHandler;

namespace xoj::circuit {

class CustomShapeManager {
public:
    CustomShapeManager();
    virtual ~CustomShapeManager() = default;

    void loadDefaults();
    void loadFromDirectory(const fs::path& directory);
    auto importSvgFile(const fs::path& filePath) -> std::shared_ptr<CircuitTemplate>;
    auto importSvgString(const std::string& id, const std::string& displayName, const std::string& svgContent)
            -> std::shared_ptr<CircuitTemplate>;

    void addTemplate(std::shared_ptr<CircuitTemplate> tpl);
    [[nodiscard]] auto getTemplates() const -> const std::vector<std::shared_ptr<CircuitTemplate>>&;
    [[nodiscard]] auto getTemplateById(const std::string& id) const -> CircuitTemplate*;

    auto recognize(const Stroke* stroke, CircuitRecognitionResult* outResult = nullptr, double threshold = 0.65)
            -> std::unique_ptr<Stroke>;

    auto recognizeComposite(const Stroke* stroke, CircuitRecognitionResult* outResult = nullptr, double threshold = 0.65)
            -> std::vector<std::unique_ptr<Stroke>>;

    auto snapShape(const CircuitTemplate* tpl, const Point& startPt, const Point& endPt, const Stroke* styleSource,
                   bool orthoSnap = true, SnapToGridInputHandler* snappingHandler = nullptr) -> std::unique_ptr<Stroke>;

    auto snapShapeComposite(const CircuitTemplate* tpl, const Point& startPt, const Point& endPt, const Stroke* styleSource,
                            bool orthoSnap = true, SnapToGridInputHandler* snappingHandler = nullptr)
            -> std::vector<std::unique_ptr<Stroke>>;

private:
    std::vector<std::shared_ptr<CircuitTemplate>> templates;
};

}  // namespace xoj::circuit
