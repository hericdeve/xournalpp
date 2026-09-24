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

#include <optional>
#include <vector>

#include <gtk/gtk.h>  // for GtkWidget

#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "util/Color.h"       // for Color
#include "util/NamedColor.h"  // for NamedColor
#include "util/Recolor.h"     // for Recolor

#include "AbstractToolItem.h"  // for AbstractToolItem

class ActionDatabase;

class ColorToolItem: public AbstractToolItem {
public:
    ColorToolItem(NamedColor namedColor, const std::optional<Recolor>& recolor);
    ColorToolItem(ColorToolItem const&) = delete;
    ColorToolItem(ColorToolItem&&) noexcept = delete;
    auto operator=(ColorToolItem const&) -> ColorToolItem& = delete;
    auto operator=(ColorToolItem&&) noexcept -> ColorToolItem& = delete;
    ~ColorToolItem() override;


public:
    std::string getToolDisplayName() const override;
    GtkWidget* getNewToolIcon() const override;

    Color getColor() const;
    Color getDisplayedColor() const;

    /**
     * @brief Update Color based on (new) palette
     *
     * @param palette
     */
    void updateColor(const Palette& palette);

    /**
     * @brief Update displayed color based on (new) recoloring settings
     *
     * @param recolor
     */
    void updateRecolor(const std::optional<Recolor>& recolor);
    void updateSecondaryColor(const std::optional<Recolor>& recolor);

    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    NamedColor namedColor;
    xoj::util::GVariantSPtr target;       ///< Contains the color in ARGB as a uint32_t
    std::optional<Recolor> recolor;       ///< Active recolor settings for canvas/theme
    std::vector<GtkWidget*> buttons;      ///< Created buttons for live icon updates
    std::vector<GtkWidget*> proxyIcons;   ///< Created proxy menu icons for live icon updates
};
