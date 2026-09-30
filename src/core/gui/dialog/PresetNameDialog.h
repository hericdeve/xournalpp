/*
 * Xournal++
 *
 * Dialog for naming / renaming a Pen Preset
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>

#include <gtk/gtk.h>

#include "model/LineStyle.h"
#include "model/PenPreset.h"
#include "util/raii/GtkWindowUPtr.h"

class GladeSearchpath;

class PresetNameDialog {
public:
    using Callback = std::function<void(const std::string& name)>;

    PresetNameDialog(GladeSearchpath* gladeSearchPath, const std::string& initialName, const PenPreset& preset,
                     Callback callback);
    ~PresetNameDialog() = default;

    GtkWindow* getWindow() const { return window.get(); }

private:
    static gboolean onDrawPreview(GtkWidget* widget, cairo_t* cr, gpointer data);

    xoj::util::GtkWindowUPtr window;
    GtkEntry* nameEntry = nullptr;
    GtkWidget* previewArea = nullptr;
    PenPreset preset;
    Callback callback;
};
