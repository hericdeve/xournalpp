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

#include "PenPresetsToolButton.h"

#include <utility>

#include <gtk/gtk.h>

#include "control/Control.h"
#include "gui/MainWindow.h"
#include "gui/dialog/PresetNameDialog.h"
#include "util/GtkUtil.h"
#include "util/PopupWindowWrapper.h"
#include "util/gtk4_helper.h"
#include "util/i18n.h"

namespace {

struct PresetCallbackData {
    PenPresetManager* manager;
    size_t index;
};

void onSelectPreset(GtkButton*, gpointer user_data) {
    auto* data = static_cast<PresetCallbackData*>(user_data);
    data->manager->applyPreset(data->index);
}

void onDeletePreset(GtkButton*, gpointer user_data) {
    auto* data = static_cast<PresetCallbackData*>(user_data);
    data->manager->removePreset(data->index);
}

void onRenamePreset(GtkButton* btn, gpointer user_data) {
    auto* self = static_cast<PenPresetsToolButton*>(user_data);
    auto* mgr = self->getControl()->getPenPresetManager();
    if (!mgr) {
        return;
    }

    size_t idx = static_cast<size_t>(GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(btn), "preset_idx")));
    auto currentPresets = mgr->getPresets();
    if (idx >= currentPresets.size()) {
        return;
    }
    auto p = currentPresets[idx];

    auto popup = std::make_unique<xoj::popup::PopupWindowWrapper<PresetNameDialog>>(
            self->getControl()->getGladeSearchPath(), p.name, p, [mgr, idx](const std::string& newName) {
                mgr->renamePreset(idx, newName);
            });
    popup->show(self->getControl()->getGtkWindow());
    popup.release();
}

void onSaveCurrentTool(GtkButton*, gpointer user_data) {
    auto* self = static_cast<PenPresetsToolButton*>(user_data);
    if (!self->getControl() || !self->getControl()->getPenPresetManager()) {
        return;
    }

    auto* mgr = self->getControl()->getPenPresetManager();
    auto preset = mgr->createPresetFromCurrent(_("Custom Preset"));

    auto popup = std::make_unique<xoj::popup::PopupWindowWrapper<PresetNameDialog>>(
            self->getControl()->getGladeSearchPath(), preset.name, preset, [mgr, preset](const std::string& name) {
                PenPreset p = preset;
                p.name = name;
                mgr->addPreset(p);
            });
    popup->show(self->getControl()->getGtkWindow());
    popup.release();
}

}  // namespace

PenPresetsToolButton::PenPresetsToolButton(Control* control, std::string id, Category cat, std::string iconName,
                                           std::string description):
        AbstractToolItem(std::move(id), cat),
        control(control),
        iconName(std::move(iconName)),
        description(std::move(description)) {
    if (this->control && this->control->getPenPresetManager()) {
        this->control->getPenPresetManager()->addListener(this);
    }
}

PenPresetsToolButton::~PenPresetsToolButton() {
    if (this->control && this->control->getPenPresetManager()) {
        this->control->getPenPresetManager()->removeListener(this);
    }
}

GtkWidget* PenPresetsToolButton::getNewToolIcon() const {
    return gtk_image_new_from_icon_name(iconName.c_str(), GTK_ICON_SIZE_LARGE_TOOLBAR);
}

std::string PenPresetsToolButton::getToolDisplayName() const { return description; }

void PenPresetsToolButton::onPresetsChanged() {
    if (activeListContainer && activePopover) {
        populatePresetsList(activeListContainer, activePopover);
    }
}

void PenPresetsToolButton::populatePresetsList(GtkWidget* box, GtkWidget* popover) {
    // Clear existing rows
    GList* children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList* iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    if (!control || !control->getPenPresetManager()) {
        return;
    }

    auto* manager = control->getPenPresetManager();
    const auto& presets = manager->getPresets();

    for (size_t i = 0; i < presets.size(); ++i) {
        const auto& preset = presets[i];

        GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(rowBox, 4);
        gtk_widget_set_margin_end(rowBox, 4);
        gtk_widget_set_margin_top(rowBox, 2);
        gtk_widget_set_margin_bottom(rowBox, 2);

        // Color badge drawing area
        GtkWidget* swatch = gtk_drawing_area_new();
        gtk_widget_set_size_request(swatch, 16, 16);
        Color col = preset.color;
        g_signal_connect(swatch, "draw",
                         G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer user_data) -> gboolean {
                             auto colorVal = reinterpret_cast<uintptr_t>(user_data);
                             Color c(static_cast<uint32_t>(colorVal));
                             cairo_arc(cr, 8, 8, 7, 0, 2 * G_PI);
                             cairo_set_source_rgb(cr, c.red / 255.0, c.green / 255.0, c.blue / 255.0);
                             cairo_fill_preserve(cr);
                             cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
                             cairo_set_line_width(cr, 1.0);
                             cairo_stroke(cr);
                             return FALSE;
                         }),
                         reinterpret_cast<gpointer>(static_cast<uintptr_t>(uint32_t(col))));

        // Button to activate preset
        GtkWidget* selectBtn = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(selectBtn), GTK_RELIEF_NONE);
        GtkWidget* btnContent = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_append(GTK_BOX(btnContent), swatch);

        GtkWidget* label = gtk_label_new(preset.name.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_box_append(GTK_BOX(btnContent), label);

        gtk_container_add(GTK_CONTAINER(selectBtn), btnContent);
        gtk_widget_set_hexpand(selectBtn, TRUE);

        auto* selectData = new PresetCallbackData{manager, i};
        g_object_set_data_full(G_OBJECT(selectBtn), "cb_data", selectData,
                               [](gpointer d) { delete static_cast<PresetCallbackData*>(d); });
        g_signal_connect(selectBtn, "clicked", G_CALLBACK(onSelectPreset), selectData);
        g_signal_connect_swapped(selectBtn, "clicked", G_CALLBACK(gtk_popover_popdown), popover);

        gtk_box_append(GTK_BOX(rowBox), selectBtn);

        // Rename button
        GtkWidget* renameBtn = gtk_button_new_from_icon_name("document-edit-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_relief(GTK_BUTTON(renameBtn), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(renameBtn, _("Rename preset"));
        g_object_set_data(G_OBJECT(renameBtn), "preset_idx", GSIZE_TO_POINTER(i));
        g_signal_connect(renameBtn, "clicked", G_CALLBACK(onRenamePreset), this);

        gtk_box_append(GTK_BOX(rowBox), renameBtn);

        // Delete button
        GtkWidget* deleteBtn = gtk_button_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_relief(GTK_BUTTON(deleteBtn), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(deleteBtn, _("Delete preset"));

        auto* deleteData = new PresetCallbackData{manager, i};
        g_object_set_data_full(G_OBJECT(deleteBtn), "cb_data", deleteData,
                               [](gpointer d) { delete static_cast<PresetCallbackData*>(d); });
        g_signal_connect(deleteBtn, "clicked", G_CALLBACK(onDeletePreset), deleteData);

        gtk_box_append(GTK_BOX(rowBox), deleteBtn);

        gtk_box_append(GTK_BOX(box), rowBox);
    }

    gtk_widget_show_all(box);
}

GtkWidget* PenPresetsToolButton::createPresetsPopover(GtkWidget* relativeTo) {
    GtkWidget* popover = gtk_popover_new(relativeTo);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(mainBox, 8);
    gtk_widget_set_margin_end(mainBox, 8);
    gtk_widget_set_margin_top(mainBox, 8);
    gtk_widget_set_margin_bottom(mainBox, 8);

    // Header
    GtkWidget* headerLabel = gtk_label_new(_("Pen Presets"));
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(headerLabel), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_append(GTK_BOX(mainBox), headerLabel);

    // List container
    GtkWidget* listContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    this->activeListContainer = listContainer;
    this->activePopover = popover;
    populatePresetsList(listContainer, popover);
    gtk_box_append(GTK_BOX(mainBox), listContainer);

    // Separator
    gtk_box_append(GTK_BOX(mainBox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // "+ Add Current Pen as Preset..."
    GtkWidget* saveCurrentBtn = gtk_button_new_with_label(_("+ Add Current Pen as Preset..."));
    gtk_button_set_relief(GTK_BUTTON(saveCurrentBtn), GTK_RELIEF_NONE);
    g_signal_connect(saveCurrentBtn, "clicked", G_CALLBACK(onSaveCurrentTool), this);
    g_signal_connect_swapped(saveCurrentBtn, "clicked", G_CALLBACK(gtk_popover_popdown), popover);

    gtk_box_append(GTK_BOX(mainBox), saveCurrentBtn);

    gtk_container_add(GTK_CONTAINER(popover), mainBox);
    gtk_widget_show_all(mainBox);

    return popover;
}

auto PenPresetsToolButton::createItem(bool horizontal) -> xoj::util::WidgetSPtr {
    GtkWidget* btn = gtk_menu_button_new();
    gtk_widget_set_can_focus(btn, false);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(btn), getNewToolIcon());
    gtk_widget_set_tooltip_text(btn, getToolDisplayName().c_str());
    gtk_menu_button_set_direction(GTK_MENU_BUTTON(btn), horizontal ? GTK_ARROW_DOWN : GTK_ARROW_RIGHT);

    GtkWidget* popover = createPresetsPopover(btn);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn), popover);

    GtkToolItem* it = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(it), btn);

    g_signal_connect_object(
            it, "toolbar-reconfigured",
            G_CALLBACK(+[](GtkToolItem* item, gpointer button) {
                GtkOrientation orientation = gtk_tool_item_get_orientation(item);
                gtk_menu_button_set_direction(GTK_MENU_BUTTON(button), orientation == GTK_ORIENTATION_HORIZONTAL ?
                                                                               GTK_ARROW_DOWN :
                                                                               GTK_ARROW_RIGHT);
            }),
            btn, G_CONNECT_DEFAULT);

    gtk_widget_show_all(GTK_WIDGET(it));
    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}
