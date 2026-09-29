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

#pragma once

#include <functional>
#include <string>

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "AbstractToolItem.h"

class RecentDocumentsToolButton: public AbstractToolItem {
public:
    using MenuModelSupplier = std::function<GMenuModel*()>;

    RecentDocumentsToolButton(std::string id, Category cat, std::string iconName, std::string description,
                              MenuModelSupplier supplier);
    ~RecentDocumentsToolButton() override = default;

    GtkWidget* getNewToolIcon() const override;
    std::string getToolDisplayName() const override;

    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    std::string iconName;
    std::string description;
    MenuModelSupplier menuModelSupplier;
};
