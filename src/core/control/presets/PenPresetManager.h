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

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "control/ToolEnums.h"
#include "control/settings/Settings.h"
#include "model/PenPreset.h"

class Control;

class PenPresetListener {
public:
    virtual ~PenPresetListener() = default;
    virtual void onPresetsChanged() = 0;
};

class PenPresetManager {
public:
    explicit PenPresetManager(Control* control, Settings* settings);
    ~PenPresetManager() = default;

    void addListener(PenPresetListener* listener);
    void removeListener(PenPresetListener* listener);

    const std::vector<PenPreset>& getPresets() const;
    void addPreset(const PenPreset& preset);
    void updatePreset(size_t index, const PenPreset& preset);
    void removePreset(size_t index);
    void renamePreset(size_t index, const std::string& newName);

    void applyPreset(const PenPreset& preset);
    void applyPreset(size_t index);

    PenPreset createPresetFromCurrent(const std::string& name) const;

private:
    void initDefaultPresetsIfNeeded();
    void firePresetsChanged();

    Control* control;
    Settings* settings;
    std::vector<PenPreset> presets;
    std::vector<PenPresetListener*> listeners;
};
