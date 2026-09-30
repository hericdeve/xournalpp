/*
 * Xournal++
 *
 * Unit tests for Zoom & Scroll lock states and clamping
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/ScrollHandler.h"
#include "control/zoom/ZoomControl.h"

TEST(ZoomScrollLockTest, testZoomLockState) {
    ZoomControl zoom;
    EXPECT_FALSE(zoom.isZoomLocked());

    zoom.setZoomLocked(true);
    EXPECT_TRUE(zoom.isZoomLocked());

    zoom.setZoomLocked(false);
    EXPECT_FALSE(zoom.isZoomLocked());
}

TEST(ZoomScrollLockTest, testScrollLockStates) {
    ScrollHandler scrollHandler(nullptr);
    EXPECT_FALSE(scrollHandler.isHorizontalScrollLocked());
    EXPECT_FALSE(scrollHandler.isScrollLocked());

    scrollHandler.setHorizontalScrollLocked(true);
    EXPECT_TRUE(scrollHandler.isHorizontalScrollLocked());
    EXPECT_FALSE(scrollHandler.isScrollLocked());

    scrollHandler.setScrollLocked(true);
    EXPECT_TRUE(scrollHandler.isScrollLocked());

    scrollHandler.setHorizontalScrollLocked(false);
    EXPECT_FALSE(scrollHandler.isHorizontalScrollLocked());
    EXPECT_TRUE(scrollHandler.isScrollLocked());

    scrollHandler.setScrollLocked(false);
    EXPECT_FALSE(scrollHandler.isScrollLocked());
}
