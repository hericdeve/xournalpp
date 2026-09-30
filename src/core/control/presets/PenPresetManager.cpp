/*
 * Xournal++
 *
 * Controller managing pen presets
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "PenPresetManager.h"

#include <algorithm>
#include <utility>

#include "control/Control.h"
#include "control/ToolHandler.h"
#include "util/i18n.h"

PenPresetManager::PenPresetManager(Control* control, Settings* settings): control(control), settings(settings) {
    if (settings) {
        this->presets = settings->getPenPresets();
    }
    initDefaultPresetsIfNeeded();
}

void PenPresetManager::initDefaultPresetsIfNeeded() {
    if (!this->presets.empty()) {
        return;
    }

    // Default presets matching popular note-taking styles
    this->presets.emplace_back("preset_black_pen", _("Black Pen"), TOOL_PEN, TOOL_SIZE_FINE, Color(0x000000));
    this->presets.emplace_back("preset_blue_pen", _("Blue Pen"), TOOL_PEN, TOOL_SIZE_FINE, Color(0x0055ff));
    this->presets.emplace_back("preset_red_pen", _("Red Pen"), TOOL_PEN, TOOL_SIZE_FINE, Color(0xff0000));
    this->presets.emplace_back("preset_highlighter", _("Highlighter"), TOOL_HIGHLIGHTER, TOOL_SIZE_MEDIUM,
                               Color(0xffff00));

    if (settings) {
        settings->setPenPresets(this->presets);
    }
}

void PenPresetManager::addListener(PenPresetListener* listener) {
    if (listener && std::find(listeners.begin(), listeners.end(), listener) == listeners.end()) {
        listeners.push_back(listener);
    }
}

void PenPresetManager::removeListener(PenPresetListener* listener) {
    listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
}

void PenPresetManager::firePresetsChanged() {
    if (settings) {
        settings->setPenPresets(this->presets);
    }
    for (auto* l: listeners) {
        l->onPresetsChanged();
    }
}

const std::vector<PenPreset>& PenPresetManager::getPresets() const { return this->presets; }

void PenPresetManager::addPreset(const PenPreset& preset) {
    this->presets.push_back(preset);
    firePresetsChanged();
}

void PenPresetManager::updatePreset(size_t index, const PenPreset& preset) {
    if (index < this->presets.size()) {
        this->presets[index] = preset;
        firePresetsChanged();
    }
}

void PenPresetManager::removePreset(size_t index) {
    if (index < this->presets.size()) {
        this->presets.erase(this->presets.begin() + index);
        firePresetsChanged();
    }
}

void PenPresetManager::renamePreset(size_t index, const std::string& newName) {
    if (index < this->presets.size()) {
        this->presets[index].name = newName;
        firePresetsChanged();
    }
}

void PenPresetManager::applyPreset(const PenPreset& preset) {
    if (!control) {
        return;
    }
    ToolHandler* handler = control->getToolHandler();
    if (!handler) {
        return;
    }

    // 1. Select the tool type (Pen or Highlighter)
    control->selectTool(preset.toolType);

    // 2. Set color
    handler->setColor(preset.color, true);

    // 3. Set size
    handler->setSize(preset.size);

    // 4. Set line style
    handler->setLineStyle(preset.lineStyle);

    // 5. Set drawing type (freehand, ruler, rect, etc.)
    handler->setDrawingType(preset.drawingType);

    // 6. Set fill
    handler->setFillEnabled(preset.fillEnabled);
    if (preset.fillEnabled) {
        if (preset.toolType == TOOL_PEN) {
            handler->setPenFill(preset.fillAlpha);
        } else if (preset.toolType == TOOL_HIGHLIGHTER) {
            handler->setHighlighterFill(preset.fillAlpha);
        }
    }
}

void PenPresetManager::applyPreset(size_t index) {
    if (index < this->presets.size()) {
        applyPreset(this->presets[index]);
    }
}

PenPreset PenPresetManager::createPresetFromCurrent(const std::string& name) const {
    PenPreset preset;
    preset.id = "preset_" + std::to_string(g_get_real_time());
    preset.name = name;

    if (!control) {
        return preset;
    }
    ToolHandler* handler = control->getToolHandler();
    if (!handler) {
        return preset;
    }

    preset.toolType = handler->getToolType();
    if (preset.toolType != TOOL_PEN && preset.toolType != TOOL_HIGHLIGHTER) {
        // Default to pen if current tool is eraser or something else
        preset.toolType = TOOL_PEN;
    }
    preset.color = handler->getColor();
    preset.size = handler->getSize();
    preset.lineStyle = handler->getLineStyle();
    preset.drawingType = handler->getDrawingType();
    preset.fillEnabled = (handler->getFill() > 0);
    preset.fillAlpha = preset.fillEnabled ? handler->getFill() : 128;

    return preset;
}
