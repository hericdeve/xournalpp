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

#include <config-test.h>
#include <gtest/gtest.h>

#include "model/Stroke.h"
#include "model/StrokeStyle.h"

namespace {
constexpr double dashLinePattern[] = {6, 3};
constexpr double dashDotLinePattern[] = {6, 3, 0.5, 3};
constexpr double dotLinePattern[] = {0.5, 3};
constexpr double custPattern[] = {0.01, -1.0, 0.005};
}  // namespace

TEST(StrokeStyle, testParseStyle) {
    EXPECT_EQ(StrokeStyle::parseStyle("random"), LineStyle());
    EXPECT_EQ(StrokeStyle::parseStyle("cust: "), LineStyle());

    LineStyle l1;
    double testData[] = {100, 200, 300};
    l1.setDashes(std::vector<double>(testData, testData + 3));
    EXPECT_EQ(StrokeStyle::parseStyle("cust: 100 200 300"), l1);
}

TEST(StrokeStyle, testFormatStyle) {
    LineStyle dashLine;
    dashLine.setDashes(std::vector<double>(dashLinePattern, dashLinePattern + 2));
    EXPECT_EQ(StrokeStyle::formatStyle(dashLine), "dash");

    LineStyle dashDot;
    dashDot.setDashes(std::vector<double>(dashDotLinePattern, dashDotLinePattern + 4));
    EXPECT_EQ(StrokeStyle::formatStyle(dashDot), "dashdot");

    LineStyle dotLine;
    dotLine.setDashes(std::vector<double>(dotLinePattern, dotLinePattern + 2));
    EXPECT_EQ(StrokeStyle::formatStyle(dotLine), "dot");

    LineStyle custLine;
    custLine.setDashes(std::vector<double>(custPattern, custPattern + 3));
    EXPECT_EQ(StrokeStyle::formatStyle(custLine), "cust: 0.01 -1.00 0.01");
}

TEST(StrokeTest, testScaleRestoreLineWidth) {
    Stroke s;
    s.setWidth(2.5);
    s.addPoint(Point(10.0, 10.0));
    s.addPoint(Point(30.0, 30.0));

    // When restoreLineWidth is true, width should be preserved on scaling
    s.scale(10.0, 10.0, 2.0, 2.0, 0, true);
    EXPECT_DOUBLE_EQ(s.getWidth(), 2.5);

    // When restoreLineWidth is false, width should scale proportionally
    s.scale(10.0, 10.0, 2.0, 2.0, 0, false);
    EXPECT_DOUBLE_EQ(s.getWidth(), 5.0);
}
