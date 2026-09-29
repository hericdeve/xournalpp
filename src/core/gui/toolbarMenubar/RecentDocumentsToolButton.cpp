/*
 * Xournal++
 *
 * Part of the customizable toolbars
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "RecentDocumentsToolButton.h"

#include <utility>

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "util/GtkUtil.h"
#include "util/gtk4_helper.h"
#include "util/i18n.h"

RecentDocumentsToolButton::RecentDocumentsToolButton(std::string id, Category cat, std::string iconName,
                                                     std::string description, MenuModelSupplier supplier):
        AbstractToolItem(std::move(id), cat),
        iconName(std::move(iconName)),
        description(std::move(description)),
        menuModelSupplier(std::move(supplier)) {}

auto RecentDocumentsToolButton::createItem(bool horizontal) -> xoj::util::WidgetSPtr {
    GtkWidget* btn = gtk_menu_button_new();
    gtk_widget_set_can_focus(btn, false);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(btn), getNewToolIcon());
    gtk_widget_set_tooltip_text(btn, getToolDisplayName().c_str());
    gtk_menu_button_set_direction(GTK_MENU_BUTTON(btn), horizontal ? GTK_ARROW_DOWN : GTK_ARROW_RIGHT);

    GMenuModel* model = menuModelSupplier ? menuModelSupplier() : nullptr;
    if (model) {
        gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(btn), model);
    } else if (menuModelSupplier) {
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer user_data) {
            auto* self = static_cast<RecentDocumentsToolButton*>(user_data);
            if (auto* mb = GTK_MENU_BUTTON(button)) {
                if (!gtk_menu_button_get_menu_model(mb)) {
                    if (auto* m = self->menuModelSupplier ? self->menuModelSupplier() : nullptr) {
                        gtk_menu_button_set_menu_model(mb, m);
                    }
                }
            }
        }), this);
    }

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

    auto createProxy = [this]() {
        GtkWidget* proxy = gtk_menu_item_new();
        auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_container_add(GTK_CONTAINER(proxy), box);
        gtk_box_append(GTK_BOX(box), getNewToolIcon());
        gtk_box_append(GTK_BOX(box), gtk_label_new(getToolDisplayName().c_str()));

        GMenuModel* m = menuModelSupplier ? menuModelSupplier() : nullptr;
        if (m) {
            GtkWidget* sub = gtk_menu_new_from_model(m);
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(proxy), sub);
        }
        return proxy;
    };
    gtk_tool_item_set_proxy_menu_item(it, "", createProxy());

    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}

auto RecentDocumentsToolButton::getToolDisplayName() const -> std::string { return description; }

auto RecentDocumentsToolButton::getNewToolIcon() const -> GtkWidget* {
    return gtk_image_new_from_icon_name(iconName.c_str(), GTK_ICON_SIZE_LARGE_TOOLBAR);
}
