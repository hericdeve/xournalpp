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

#include "gui/toolbarMenubar/RecentDocumentsToolButton.h"

TEST(RecentDocumentsToolButtonTest, testProperties) {
    RecentDocumentsToolButton button("RECENT_DOCUMENTS", AbstractToolItem::Category::FILES, "document-open-recent",
                                     "Recent Documents", nullptr);

    EXPECT_EQ(button.getId(), "RECENT_DOCUMENTS");
    EXPECT_EQ(button.getCategory(), AbstractToolItem::Category::FILES);
    EXPECT_EQ(button.getToolDisplayName(), "Recent Documents");
}
