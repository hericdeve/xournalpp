#include <gtest/gtest.h>

#include "control/shaperecognizer/custom/AutoShapeSelection.h"
#include "control/shaperecognizer/custom/CircuitFeatureClassifier.h"
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

    // Create synthetic zig-zag resistor stroke with long leads
    Stroke resistor;
    resistor.addPoint(Point(0.0, 50.0));
    resistor.addPoint(Point(35.0, 50.0));
    resistor.addPoint(Point(42.0, 32.0));
    resistor.addPoint(Point(52.0, 68.0));
    resistor.addPoint(Point(62.0, 32.0));
    resistor.addPoint(Point(72.0, 68.0));
    resistor.addPoint(Point(80.0, 50.0));
    resistor.addPoint(Point(120.0, 50.0));

    auto snapped = mgr.recognize(&resistor, nullptr, 0.50);
    EXPECT_NE(snapped, nullptr);
}

TEST(AutoShapeSelectionTest, TestHandwritingProtection) {
    // 1. Text character '3' (compact bounding box, moderate height and width)
    Stroke digit3;
    digit3.addPoint(Point(100.0, 100.0));
    digit3.addPoint(Point(115.0, 100.0));
    digit3.addPoint(Point(110.0, 108.0));
    digit3.addPoint(Point(116.0, 116.0));
    digit3.addPoint(Point(100.0, 116.0));

    EXPECT_TRUE(CircuitFeatureClassifier::isHandwritingOrAnnotation(&digit3));

    // 2. Text character '4'
    Stroke digit4;
    digit4.addPoint(Point(120.0, 100.0));
    digit4.addPoint(Point(115.0, 110.0));
    digit4.addPoint(Point(125.0, 110.0));

    EXPECT_TRUE(CircuitFeatureClassifier::isHandwritingOrAnnotation(&digit4));

    // 3. Dot or tiny scribble
    Stroke dot;
    dot.addPoint(Point(50.0, 50.0));
    dot.addPoint(Point(52.0, 51.0));

    EXPECT_TRUE(CircuitFeatureClassifier::isHandwritingOrAnnotation(&dot));
}

}  // namespace xoj::circuit
