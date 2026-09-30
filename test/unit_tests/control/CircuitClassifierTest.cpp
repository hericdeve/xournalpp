/*
 * Xournal++
 *
 * Tests for Circuit Feature Classifier and Discriminator
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/shaperecognizer/custom/CircuitFeatureClassifier.h"
#include "control/shaperecognizer/custom/CircuitRecognizer.h"
#include "control/shaperecognizer/custom/CircuitSnapper.h"
#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "model/Stroke.h"

using namespace xoj::circuit;

TEST(CircuitClassifierTest, TestResistorFeatureDiscrimination) {
    // Generate an oscillating stroke (zig-zag resistor: alternating peaks and valleys)
    auto stroke = std::make_unique<Stroke>();
    stroke->addPoint(Point(0.0, 50.0));
    stroke->addPoint(Point(20.0, 50.0)); // lead 1

    // 4 alternating zig-zag teeth
    stroke->addPoint(Point(25.0, 30.0)); // peak 1
    stroke->addPoint(Point(35.0, 70.0)); // valley 1
    stroke->addPoint(Point(45.0, 30.0)); // peak 2
    stroke->addPoint(Point(55.0, 70.0)); // valley 2
    stroke->addPoint(Point(65.0, 30.0)); // peak 3
    stroke->addPoint(Point(75.0, 70.0)); // valley 3
    stroke->addPoint(Point(85.0, 30.0)); // peak 4
    stroke->addPoint(Point(95.0, 70.0)); // valley 4

    stroke->addPoint(Point(100.0, 50.0));
    stroke->addPoint(Point(120.0, 50.0)); // lead 2

    auto featClass = CircuitFeatureClassifier::classify(stroke.get());
    EXPECT_EQ(featClass, CircuitFeatureClass::ResistorIeee);

    // Verify it is NEVER recognized as a capacitor
    CustomShapeManager mgr;
    CircuitRecognitionResult res;
    auto snapped = mgr.recognize(stroke.get(), &res);

    ASSERT_NE(snapped, nullptr);
    ASSERT_NE(res.matchedTemplate, nullptr);
    EXPECT_TRUE(res.matchedTemplate->getId() == "resistor_ieee" || res.matchedTemplate->getId() == "resistor_iec");
    EXPECT_NE(res.matchedTemplate->getId(), "capacitor");
}

TEST(CircuitClassifierTest, TestInductorFeatureDiscrimination) {
    // Generate unipolar bumps (inductor coils, all peaks on one side)
    auto stroke = std::make_unique<Stroke>();
    stroke->addPoint(Point(0.0, 50.0));
    stroke->addPoint(Point(15.0, 50.0));

    // Coil 1
    stroke->addPoint(Point(25.0, 30.0));
    stroke->addPoint(Point(35.0, 50.0));

    // Coil 2
    stroke->addPoint(Point(45.0, 30.0));
    stroke->addPoint(Point(55.0, 50.0));

    // Coil 3
    stroke->addPoint(Point(65.0, 30.0));
    stroke->addPoint(Point(75.0, 50.0));

    // Coil 4
    stroke->addPoint(Point(85.0, 30.0));
    stroke->addPoint(Point(95.0, 50.0));

    stroke->addPoint(Point(110.0, 50.0));

    auto featClass = CircuitFeatureClassifier::classify(stroke.get());
    EXPECT_EQ(featClass, CircuitFeatureClass::Inductor);

    CustomShapeManager mgr;
    CircuitRecognitionResult res;
    auto snapped = mgr.recognize(stroke.get(), &res);

    ASSERT_NE(snapped, nullptr);
    ASSERT_NE(res.matchedTemplate, nullptr);
    EXPECT_EQ(res.matchedTemplate->getId(), "inductor");
}

TEST(CircuitClassifierTest, TestCapacitorCompositeAirGap) {
    CustomShapeManager mgr;
    auto capTpl = mgr.getTemplateById("capacitor");
    ASSERT_NE(capTpl, nullptr);

    auto styleSource = std::make_unique<Stroke>();
    styleSource->addPoint(Point(0.0, 0.0));
    styleSource->addPoint(Point(10.0, 0.0));

    auto composite = CircuitSnapper::snapCircuitComposite(capTpl, Point(10.0, 50.0), Point(90.0, 50.0), styleSource.get(), true);
    // Capacitor composite must consist of 2 disjoint strokes with an air gap
    EXPECT_EQ(composite.size(), 2);
}

TEST(CircuitClassifierTest, TestGroundCompositeGeneration) {
    CustomShapeManager mgr;
    auto groundTpl = mgr.getTemplateById("ground");
    ASSERT_NE(groundTpl, nullptr);

    auto styleSource = std::make_unique<Stroke>();
    styleSource->addPoint(Point(0.0, 0.0));
    styleSource->addPoint(Point(10.0, 0.0));

    auto composite = CircuitSnapper::snapCircuitComposite(groundTpl, Point(50.0, 10.0), Point(50.0, 60.0), styleSource.get(), true);
    // Ground composite must consist of stem + 3 descending horizontal bars = 4 strokes
    EXPECT_EQ(composite.size(), 4);
}
