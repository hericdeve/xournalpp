/*
 * Xournal++
 *
 * Popover menu and toolbar item for Pen Presets
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <string>

#include <gtk/gtk.h>

#include "AbstractToolItem.h"
#include "control/presets/PenPresetManager.h"

class Control;

class PenPresetsToolButton: public AbstractToolItem, public PenPresetListener {
public:
    PenPresetsToolButton(Control* control, std::string id, Category cat, std::string iconName,
                         std::string description);
    ~PenPresetsToolButton() override;

    GtkWidget* getNewToolIcon() const override;
    std::string getToolDisplayName() const override;

    Control* getControl() const { return control; }

    xoj::util::WidgetSPtr createItem(bool horizontal) override;

    void onPresetsChanged() override;

private:
    GtkWidget* createPresetsPopover(GtkWidget* relativeTo);
    void populatePresetsList(GtkWidget* box, GtkWidget* popover);

    Control* control;
    std::string iconName;
    std::string description;
    GtkWidget* activePopover = nullptr;
    GtkWidget* activeListContainer = nullptr;
};
