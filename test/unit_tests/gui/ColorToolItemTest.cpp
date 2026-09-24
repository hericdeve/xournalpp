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

#include <gtest/gtest.h>

#include "gui/toolbarMenubar/ColorToolItem.h"
#include "gui/toolbarMenubar/icon/ColorIcon.h"
#include "util/Color.h"
#include "util/NamedColor.h"
#include "util/Recolor.h"

TEST(ColorToolItemTest, testDisplayedColorMatchesMode) {
    NamedColor blackColor(Color(0x00, 0x00, 0x00));
    NamedColor blueColor(Color(0x33, 0x33, 0xcc));

    // Light mode without recoloring
    ColorToolItem itemBlack(blackColor, std::nullopt);
    EXPECT_EQ(itemBlack.getColor(), Color(0x00, 0x00, 0x00));
    EXPECT_EQ(itemBlack.getDisplayedColor(), Color(0x00, 0x00, 0x00));

    // Dark mode recolor (maps white background to dark gray #242424, and black text to white #ffffff)
    Recolor darkRecolor(Color(0x24, 0x24, 0x24), Color(0xff, 0xff, 0xff));

    ColorToolItem itemBlackDark(blackColor, darkRecolor);
    EXPECT_EQ(itemBlackDark.getColor(), Color(0x00, 0x00, 0x00));
    EXPECT_EQ(itemBlackDark.getDisplayedColor(), darkRecolor.convertColor(Color(0x00, 0x00, 0x00)));
    // In dark mode, pure black is converted to white
    EXPECT_EQ(itemBlackDark.getDisplayedColor(), Color(0xff, 0xff, 0xff));

    // Dynamic update when switching theme
    itemBlack.updateRecolor(darkRecolor);
    EXPECT_EQ(itemBlack.getColor(), Color(0x00, 0x00, 0x00));
    EXPECT_EQ(itemBlack.getDisplayedColor(), Color(0xff, 0xff, 0xff));

    // Switch back to light mode (no recoloring)
    itemBlack.updateRecolor(std::nullopt);
    EXPECT_EQ(itemBlack.getColor(), Color(0x00, 0x00, 0x00));
    EXPECT_EQ(itemBlack.getDisplayedColor(), Color(0x00, 0x00, 0x00));
}

TEST(ColorToolItemTest, testColorIconDimensions) {
    auto pixbuf = ColorIcon::newGdkPixbuf(Color(0xff, 0x00, 0x00), 16, true);
    ASSERT_NE(pixbuf.get(), nullptr);
    EXPECT_EQ(gdk_pixbuf_get_width(pixbuf.get()), 16);
    EXPECT_EQ(gdk_pixbuf_get_height(pixbuf.get()), 16);
}
