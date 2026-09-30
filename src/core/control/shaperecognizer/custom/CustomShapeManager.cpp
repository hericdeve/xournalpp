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

#include "CustomShapeManager.h"

#include <iostream>

#include "util/PathUtil.h"

namespace xoj::circuit {

namespace {

constexpr const char* DEFAULT_RESISTOR_IEEE_SVG =
        R"(<svg viewBox="0 0 100 30"><path d="M 0,15 L 20,15 L 25,3 L 35,27 L 45,3 L 55,27 L 65,3 L 75,27 L 80,15 L 100,15" fill="none"/></svg>)";

constexpr const char* DEFAULT_RESISTOR_IEC_SVG =
        R"(<svg viewBox="0 0 100 30"><path d="M 0,15 L 25,15 L 25,5 L 75,5 L 75,25 L 25,25 L 25,15 M 75,15 L 100,15" fill="none"/></svg>)";

constexpr const char* DEFAULT_INDUCTOR_SVG =
        R"(<svg viewBox="0 0 100 30"><path d="M 0,20 L 20,20 A 7.5,10 0 0,1 35,20 A 7.5,10 0 0,1 50,20 A 7.5,10 0 0,1 65,20 A 7.5,10 0 0,1 80,20 L 100,20" fill="none"/></svg>)";

constexpr const char* DEFAULT_CAPACITOR_SVG =
        R"(<svg viewBox="0 0 100 30"><path d="M 0,15 L 44,15 L 44,3 L 44,27 L 44,15 M 56,15 L 56,3 L 56,27 L 56,15 L 100,15" fill="none"/></svg>)";

constexpr const char* DEFAULT_DIODE_SVG =
        R"(<svg viewBox="0 0 100 30"><path d="M 0,15 L 35,15 L 35,3 L 65,15 L 35,27 L 35,15 M 65,3 L 65,27 M 65,15 L 100,15" fill="none"/></svg>)";

constexpr const char* DEFAULT_GROUND_SVG =
        R"(<svg viewBox="0 0 50 50"><path d="M 25,0 L 25,20 L 5,20 L 45,20 L 25,20 M 12,28 L 38,28 M 19,36 L 31,36" fill="none"/></svg>)";

}  // namespace

CustomShapeManager::CustomShapeManager() {
    loadDefaults();
    try {
        fs::path userShapesDir = Util::getConfigSubfolder("shapes");
        loadFromDirectory(userShapesDir);
    } catch (...) {
        // Ignore error if user config path is not accessible
    }
}

void CustomShapeManager::loadDefaults() {
    templates.clear();

    auto rIeee = CircuitTemplate::fromSvgString("resistor_ieee", "Resistor (IEEE)", DEFAULT_RESISTOR_IEEE_SVG);
    if (rIeee) {
        templates.push_back(rIeee);
    }

    auto rIec = CircuitTemplate::fromSvgString("resistor_iec", "Resistor (IEC)", DEFAULT_RESISTOR_IEC_SVG);
    if (rIec) {
        templates.push_back(rIec);
    }

    auto ind = CircuitTemplate::fromSvgString("inductor", "Inductor", DEFAULT_INDUCTOR_SVG);
    if (ind) {
        templates.push_back(ind);
    }

    auto cap = CircuitTemplate::fromSvgString("capacitor", "Capacitor", DEFAULT_CAPACITOR_SVG);
    if (cap) {
        templates.push_back(cap);
    }

    auto diode = CircuitTemplate::fromSvgString("diode", "Diode", DEFAULT_DIODE_SVG);
    if (diode) {
        templates.push_back(diode);
    }

    auto gnd = CircuitTemplate::fromSvgString("ground", "Ground", DEFAULT_GROUND_SVG);
    if (gnd) {
        templates.push_back(gnd);
    }
}

void CustomShapeManager::loadFromDirectory(const fs::path& directory) {
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        return;
    }

    for (const auto& entry: fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".svg") {
            importSvgFile(entry.path());
        }
    }
}

auto CustomShapeManager::importSvgFile(const fs::path& filePath) -> std::shared_ptr<CircuitTemplate> {
    auto tpl = CircuitTemplate::fromSvgFile(filePath.string());
    if (tpl) {
        addTemplate(tpl);
    }
    return tpl;
}

auto CustomShapeManager::importSvgString(const std::string& id, const std::string& displayName,
                                        const std::string& svgContent) -> std::shared_ptr<CircuitTemplate> {
    auto tpl = CircuitTemplate::fromSvgString(id, displayName, svgContent);
    if (tpl) {
        addTemplate(tpl);
    }
    return tpl;
}

void CustomShapeManager::addTemplate(std::shared_ptr<CircuitTemplate> tpl) {
    if (!tpl) {
        return;
    }
    // Replace if exists with same id
    for (auto& existing: templates) {
        if (existing->getId() == tpl->getId()) {
            existing = tpl;
            return;
        }
    }
    templates.push_back(std::move(tpl));
}

auto CustomShapeManager::getTemplates() const -> const std::vector<std::shared_ptr<CircuitTemplate>>& {
    return templates;
}

auto CustomShapeManager::getTemplateById(const std::string& id) const -> CircuitTemplate* {
    for (const auto& tpl: templates) {
        if (tpl && tpl->getId() == id) {
            return tpl.get();
        }
    }
    return nullptr;
}

auto CustomShapeManager::recognize(const Stroke* stroke, CircuitRecognitionResult* outResult, double threshold)
        -> std::unique_ptr<Stroke> {
    auto res = CircuitRecognizer::recognize(stroke, templates, threshold);
    if (outResult) {
        *outResult = res;
    }

    if (res.matched && res.matchedTemplate) {
        return snapShape(res.matchedTemplate, res.terminalStart, res.terminalEnd, stroke, true);
    }

    return nullptr;
}

auto CustomShapeManager::snapShape(const CircuitTemplate* tpl, const Point& startPt, const Point& endPt,
                                   const Stroke* styleSource, bool orthoSnap, SnapToGridInputHandler* snappingHandler)
        -> std::unique_ptr<Stroke> {
    return CircuitSnapper::snapCircuit(tpl, startPt, endPt, styleSource, orthoSnap, snappingHandler);
}

}  // namespace xoj::circuit
