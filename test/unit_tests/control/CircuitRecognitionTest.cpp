/*
 * Xournal++
 *
 * Unit tests for SVG parsing, circuit pattern matching, and snapping
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/shaperecognizer/custom/CircuitRecognizer.h"
#include "control/shaperecognizer/custom/CircuitSnapper.h"
#include "control/shaperecognizer/custom/CircuitTemplate.h"
#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "control/shaperecognizer/svg/SvgPathParser.h"
#include "model/Point.h"
#include "model/Stroke.h"

using namespace xoj::circuit;
using namespace xoj::svg;

TEST(SvgPathParserTest, testBasicPathCommands) {
    std::string d = "M 10,20 L 30,40 H 50 V 60 Z";
    auto subpaths = SvgPathParser::parsePathData(d);
    ASSERT_EQ(subpaths.size(), 1);
    EXPECT_TRUE(subpaths[0].closed);
    ASSERT_GE(subpaths[0].points.size(), 4);
    EXPECT_DOUBLE_EQ(subpaths[0].points.front().x, 10.0);
    EXPECT_DOUBLE_EQ(subpaths[0].points.front().y, 20.0);
}

TEST(SvgPathParserTest, testCubicBezierAndArc) {
    std::string d = "M 0,0 C 10,20 30,20 40,0 A 10,10 0 0,0 60,0";
    auto subpaths = SvgPathParser::parsePathData(d);
    ASSERT_EQ(subpaths.size(), 1);
    EXPECT_GT(subpaths[0].points.size(), 10);
    EXPECT_DOUBLE_EQ(subpaths[0].points.front().x, 0.0);
    EXPECT_NEAR(subpaths[0].points.back().x, 60.0, 1.0);
}

TEST(CircuitRecognizerTest, testResistorRecognition) {
    CustomShapeManager mgr;

    // Simulate drawing a zig-zag resistor from (100, 100) to (300, 100)
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2.0);

    // Initial lead
    for (double x = 100.0; x <= 140.0; x += 5.0) {
        stroke->addPoint(Point(x, 100.0));
    }
    // Zig-zags
    stroke->addPoint(Point(150.0, 80.0));
    stroke->addPoint(Point(170.0, 120.0));
    stroke->addPoint(Point(190.0, 80.0));
    stroke->addPoint(Point(210.0, 120.0));
    stroke->addPoint(Point(230.0, 80.0));
    stroke->addPoint(Point(250.0, 120.0));
    // Ending lead
    for (double x = 260.0; x <= 300.0; x += 5.0) {
        stroke->addPoint(Point(x, 100.0));
    }

    CircuitRecognitionResult res;
    auto recognized = mgr.recognize(stroke.get(), &res, 0.60);

    ASSERT_TRUE(res.matched);
    ASSERT_NE(res.matchedTemplate, nullptr);
    EXPECT_EQ(res.matchedTemplate->getId(), "resistor_ieee");
    ASSERT_NE(recognized, nullptr);
    EXPECT_GT(recognized->getPointCount(), 8);
}

TEST(CircuitSnapperTest, testAngleAndLeadStretching) {
    // 85 degrees should snap to 90 degrees (vertical)
    double angle85 = 85.0 * (M_PI / 180.0);
    double snapped = CircuitSnapper::snapAngle(angle85);
    EXPECT_NEAR(snapped, M_PI / 2.0, 1e-6);

    // Test snapper output
    auto tpl = CircuitTemplate::fromSvgString("r", "R",
                                              R"(<svg viewBox="0 0 100 20"><path d="M 0,10 L 20,10 L 25,0 L 35,20 L 45,0 L 55,20 L 60,10 L 100,10"/></svg>)");
    ASSERT_NE(tpl, nullptr);

    Stroke styleSource;
    styleSource.setWidth(1.5);
    Point pStart(50.0, 50.0);
    Point pEnd(250.0, 50.0);

    auto snappedStroke = CircuitSnapper::snapCircuit(tpl.get(), pStart, pEnd, &styleSource, true);
    ASSERT_NE(snappedStroke, nullptr);
    EXPECT_GT(snappedStroke->getPointCount(), 6);
    EXPECT_NEAR(snappedStroke->getPointVector().front().x, 50.0, 0.1);
    EXPECT_NEAR(snappedStroke->getPointVector().back().x, 250.0, 0.1);
}

TEST(CircuitRecognizerTest, testSmallAndLargeResistorRecognition) {
    CustomShapeManager mgr;

    // 1. Small resistor: 35px total span from (10, 10) to (45, 10)
    auto smallStroke = std::make_unique<Stroke>();
    smallStroke->setWidth(1.5);
    smallStroke->addPoint(Point(10.0, 10.0));
    smallStroke->addPoint(Point(14.0, 10.0));
    // zig-zags
    smallStroke->addPoint(Point(17.0, 5.0));
    smallStroke->addPoint(Point(21.0, 15.0));
    smallStroke->addPoint(Point(25.0, 5.0));
    smallStroke->addPoint(Point(29.0, 15.0));
    smallStroke->addPoint(Point(33.0, 5.0));
    smallStroke->addPoint(Point(37.0, 15.0));
    smallStroke->addPoint(Point(41.0, 10.0));
    smallStroke->addPoint(Point(45.0, 10.0));

    CircuitRecognitionResult smallRes;
    auto smallRecognized = mgr.recognize(smallStroke.get(), &smallRes, 0.55);
    ASSERT_TRUE(smallRes.matched);
    ASSERT_NE(smallRes.matchedTemplate, nullptr);
    EXPECT_EQ(smallRes.matchedTemplate->getId(), "resistor_ieee");
    ASSERT_NE(smallRecognized, nullptr);

    // 2. Large resistor: 450px total span from (50, 200) to (500, 200)
    auto largeStroke = std::make_unique<Stroke>();
    largeStroke->setWidth(2.5);
    for (double x = 50.0; x <= 120.0; x += 10.0) {
        largeStroke->addPoint(Point(x, 200.0));
    }
    // large zig-zags
    largeStroke->addPoint(Point(150.0, 150.0));
    largeStroke->addPoint(Point(200.0, 250.0));
    largeStroke->addPoint(Point(250.0, 150.0));
    largeStroke->addPoint(Point(300.0, 250.0));
    largeStroke->addPoint(Point(350.0, 150.0));
    largeStroke->addPoint(Point(400.0, 250.0));
    for (double x = 420.0; x <= 500.0; x += 10.0) {
        largeStroke->addPoint(Point(x, 200.0));
    }

    CircuitRecognitionResult largeRes;
    auto largeRecognized = mgr.recognize(largeStroke.get(), &largeRes, 0.55);
    ASSERT_TRUE(largeRes.matched);
    ASSERT_NE(largeRes.matchedTemplate, nullptr);
    EXPECT_EQ(largeRes.matchedTemplate->getId(), "resistor_ieee");
    ASSERT_NE(largeRecognized, nullptr);
}

TEST(CircuitSnapperTest, testSnappingScalesWithDistance) {
    CustomShapeManager mgr;
    auto tpl = mgr.getTemplateById("resistor_ieee");
    ASSERT_NE(tpl, nullptr);

    Stroke style;
    style.setWidth(2.0);

    // Small snapped resistor: distance = 40
    auto small = CircuitSnapper::snapCircuit(tpl, Point(0, 0), Point(40, 0), &style, false);
    ASSERT_NE(small, nullptr);
    auto smallBbox = small->getBoundingBox();

    // Large snapped resistor: distance = 400
    auto large = CircuitSnapper::snapCircuit(tpl, Point(0, 0), Point(400, 0), &style, false);
    ASSERT_NE(large, nullptr);
    auto largeBbox = large->getBoundingBox();

    // The large resistor body must be significantly taller and wider than the small resistor!
    EXPECT_GT(largeBbox.height, smallBbox.height * 2.0);
    EXPECT_GT(largeBbox.width, smallBbox.width * 5.0);
}
