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

#include "PresetNameDialog.h"

#include <utility>

#include "gui/Builder.h"
#include "gui/GladeSearchpath.h"
#include "util/GtkUtil.h"
#include "util/gtk4_helper.h"
#include "util/i18n.h"

PresetNameDialog::PresetNameDialog(GladeSearchpath* gladeSearchPath, const std::string& initialName,
                                   const PenPreset& preset, Callback callback):
        preset(preset), callback(std::move(callback)) {
    Builder builder(gladeSearchPath, "presetNameDialog.glade");

    this->window.reset(GTK_WINDOW(builder.get("presetNameDialog")));
    this->nameEntry = GTK_ENTRY(builder.get("presetNameEntry"));
    this->previewArea = builder.get("presetPreviewArea");

    if (this->nameEntry) {
        gtk_entry_set_text(this->nameEntry, initialName.c_str());
        gtk_widget_grab_focus(GTK_WIDGET(this->nameEntry));
    }

    if (this->previewArea) {
        g_signal_connect(this->previewArea, "draw", G_CALLBACK(onDrawPreview), this);
    }

    GtkWidget* btnCancel = builder.get("btnCancel");
    if (btnCancel) {
        g_signal_connect_swapped(btnCancel, "clicked", G_CALLBACK(gtk_widget_destroy), this->window.get());
    }

    auto onSave = [this]() {
        if (this->nameEntry && this->callback) {
            const char* text = gtk_entry_get_text(this->nameEntry);
            if (text && *text) {
                this->callback(std::string(text));
            }
        }
        gtk_widget_destroy(GTK_WIDGET(this->window.get()));
    };

    GtkWidget* btnSave = builder.get("btnSave");
    if (btnSave) {
        g_signal_connect_object(
                btnSave, "clicked",
                G_CALLBACK(+[](GtkButton*, gpointer user_data) {
                    auto* fn = static_cast<std::function<void()>*>(user_data);
                    (*fn)();
                }),
                new std::function<void()>(onSave), GConnectFlags(0));
    }

    if (this->nameEntry) {
        g_signal_connect_object(
                this->nameEntry, "activate",
                G_CALLBACK(+[](GtkEntry*, gpointer user_data) {
                    auto* fn = static_cast<std::function<void()>*>(user_data);
                    (*fn)();
                }),
                new std::function<void()>(onSave), GConnectFlags(0));
    }
}

gboolean PresetNameDialog::onDrawPreview(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* self = static_cast<PresetNameDialog*>(data);
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    // Draw clean background
    cairo_set_source_rgb(cr, 0.96, 0.96, 0.97);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);

    // Stroke width based on tool size
    double width = 2.0;
    switch (self->preset.size) {
        case TOOL_SIZE_VERY_FINE:
            width = 1.0;
            break;
        case TOOL_SIZE_FINE:
            width = 2.0;
            break;
        case TOOL_SIZE_MEDIUM:
            width = 3.5;
            break;
        case TOOL_SIZE_THICK:
            width = 5.0;
            break;
        case TOOL_SIZE_VERY_THICK:
            width = 8.0;
            break;
        default:
            width = 2.0;
            break;
    }

    if (self->preset.toolType == TOOL_HIGHLIGHTER) {
        width *= 3.5;
    }

    Color c = self->preset.color;
    double r = c.red / 255.0;
    double g = c.green / 255.0;
    double b = c.blue / 255.0;
    double a = (self->preset.toolType == TOOL_HIGHLIGHTER) ? 0.45 : 1.0;

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_set_line_width(cr, width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    // Apply dashes if needed
    if (self->preset.lineStyle.hasDashes()) {
        std::vector<double> dashes;
        for (double d: self->preset.lineStyle.getDashes()) {
            dashes.push_back(d * width);
        }
        cairo_set_dash(cr, dashes.data(), static_cast<int>(dashes.size()), 0.0);
    }

    // Draw wavy preview curve
    double startX = 20.0;
    double endX = alloc.width - 20.0;
    double midY = alloc.height / 2.0;

    cairo_move_to(cr, startX, midY);
    cairo_curve_to(cr, startX + (endX - startX) * 0.3, midY - 14.0, startX + (endX - startX) * 0.7, midY + 14.0,
                   endX, midY);
    cairo_stroke(cr);

    return FALSE;
}
