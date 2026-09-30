/*
 * Xournal++
 *
 * Unit tests for PenPresetManager
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/presets/PenPresetManager.h"
#include "control/settings/Settings.h"
#include "filesystem.h"

class DummyPresetListener: public PenPresetListener {
public:
    int changeCount = 0;
    void onPresetsChanged() override { changeCount++; }
};

TEST(PenPresetManagerTest, testPresetCrudAndListeners) {
    Settings settings{"non-existing-file-path"};
    PenPresetManager manager(nullptr, &settings);

    DummyPresetListener listener;
    manager.addListener(&listener);

    size_t initialCount = manager.getPresets().size();
    EXPECT_GE(initialCount, 4UL);

    // Add preset
    PenPreset custom("custom_test", "Calligraphy Blue", TOOL_PEN, TOOL_SIZE_MEDIUM, Color(0x123456));
    manager.addPreset(custom);
    EXPECT_EQ(manager.getPresets().size(), initialCount + 1);
    EXPECT_EQ(listener.changeCount, 1);
    EXPECT_EQ(manager.getPresets().back().name, "Calligraphy Blue");

    // Rename preset
    manager.renamePreset(manager.getPresets().size() - 1, "Calligraphy Navy");
    EXPECT_EQ(listener.changeCount, 2);
    EXPECT_EQ(manager.getPresets().back().name, "Calligraphy Navy");

    // Remove preset
    manager.removePreset(manager.getPresets().size() - 1);
    EXPECT_EQ(manager.getPresets().size(), initialCount);
    EXPECT_EQ(listener.changeCount, 3);

    manager.removeListener(&listener);
}
