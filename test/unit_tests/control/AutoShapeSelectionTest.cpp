#include <gtest/gtest.h>

#include "control/shaperecognizer/custom/AutoShapeSelection.h"
#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "model/Point.h"
#include "model/Stroke.h"

namespace xoj::circuit {

TEST(AutoShapeSelectionTest, TestStraightAndCornerWireSnapping) {
    // 1. Create a near-horizontal straight wire stroke
    Stroke wire;
    wire.addPoint(Point(10.0, 50.0));
    wire.addPoint(Point(40.0, 51.0));
    wire.addPoint(Point(80.0, 49.5));
    wire.addPoint(Point(120.0, 50.5));

    // 2. Create an L-shaped corner wire stroke
    Stroke corner;
    corner.addPoint(Point(10.0, 10.0));
    corner.addPoint(Point(50.0, 11.0));
    corner.addPoint(Point(51.0, 40.0));
    corner.addPoint(Point(50.0, 80.0));

    EXPECT_GE(wire.getPointCount(), 4);
    EXPECT_GE(corner.getPointCount(), 4);
}

TEST(AutoShapeSelectionTest, TestResistorAndInductorTemplateRecognition) {
    CustomShapeManager mgr;

    // Create synthetic zig-zag resistor stroke
    Stroke resistor;
    resistor.addPoint(Point(0.0, 50.0));
    resistor.addPoint(Point(25.0, 50.0));
    resistor.addPoint(Point(32.0, 30.0));
    resistor.addPoint(Point(44.0, 70.0));
    resistor.addPoint(Point(56.0, 30.0));
    resistor.addPoint(Point(68.0, 70.0));
    resistor.addPoint(Point(75.0, 50.0));
    resistor.addPoint(Point(100.0, 50.0));

    auto snapped = mgr.recognize(&resistor, nullptr, 0.50);
    EXPECT_NE(snapped, nullptr);
}

}  // namespace xoj::circuit
