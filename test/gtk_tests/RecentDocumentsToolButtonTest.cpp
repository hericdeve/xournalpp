/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "dialog/GtkTest.h"
#include "gui/toolbarMenubar/RecentDocumentsToolButton.h"

class RecentDocumentsToolButtonGtkTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GMenu* menu = g_menu_new();
        g_menu_append(menu, "Item 1", "app.test");

        RecentDocumentsToolButton button("RECENT_DOCUMENTS", AbstractToolItem::Category::FILES, "document-open-recent",
                                         "Recent Documents", [menu]() -> GMenuModel* { return G_MENU_MODEL(menu); });

        auto item = button.createItem(true);
        ASSERT_NE(item.get(), nullptr);
        EXPECT_TRUE(GTK_IS_TOOL_ITEM(item.get()));

        GtkWidget* child = gtk_bin_get_child(GTK_BIN(item.get()));
        ASSERT_NE(child, nullptr);
        EXPECT_TRUE(GTK_IS_MENU_BUTTON(child));
        EXPECT_EQ(gtk_menu_button_get_menu_model(GTK_MENU_BUTTON(child)), G_MENU_MODEL(menu));

        g_object_unref(menu);
    }
};

TEST_F(RecentDocumentsToolButtonGtkTest, testCreateItemAndModel) {}
