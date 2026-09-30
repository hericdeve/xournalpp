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
    stroke->addPoint(Point(10.0, 50.0)); // lead 1

    // 4 alternating zig-zag teeth
    stroke->addPoint(Point(20.0, 30.0)); // peak 1
    stroke->addPoint(Point(30.0, 70.0)); // valley 1
    stroke->addPoint(Point(40.0, 30.0)); // peak 2
    stroke->addPoint(Point(50.0, 70.0)); // valley 2
    stroke->addPoint(Point(60.0, 30.0)); // peak 3
    stroke->addPoint(Point(70.0, 70.0)); // valley 3
    stroke->addPoint(Point(80.0, 30.0)); // peak 4
    stroke->addPoint(Point(90.0, 70.0)); // valley 4

    stroke->addPoint(Point(100.0, 50.0));
    stroke->addPoint(Point(110.0, 50.0)); // lead 2

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

TEST(CircuitClassifierTest, TestVerticalResistorWithLongLeads) {
    // Test a vertical branch containing a resistor with leading and trailing wire (like R1 in real drawings)
    auto stroke = std::make_unique<Stroke>();
    stroke->addPoint(Point(100.0, 50.0));
    stroke->addPoint(Point(100.0, 80.0)); // lead
    stroke->addPoint(Point(112.0, 90.0)); // peak
    stroke->addPoint(Point(88.0, 100.0)); // valley
    stroke->addPoint(Point(112.0, 110.0)); // peak
    stroke->addPoint(Point(88.0, 120.0)); // valley
    stroke->addPoint(Point(112.0, 130.0)); // peak
    stroke->addPoint(Point(100.0, 140.0));
    stroke->addPoint(Point(100.0, 180.0)); // lead

    auto featClass = CircuitFeatureClassifier::classify(stroke.get());
    EXPECT_EQ(featClass, CircuitFeatureClass::ResistorIeee);

    CustomShapeManager mgr;
    CircuitRecognitionResult res;
    auto snapped = mgr.recognize(stroke.get(), &res);
    ASSERT_NE(snapped, nullptr);
    EXPECT_EQ(res.matchedTemplate->getId(), "resistor_ieee");
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

TEST(CircuitClassifierTest, TestBjtTransistorDetection) {
    // 1. Base bar: vertical straight segment
    auto sBase = std::make_unique<Stroke>();
    sBase->addPoint(Point(50.0, 30.0));
    sBase->addPoint(Point(50.0, 70.0));

    // 2. Collector lead: slanted from (50, 40) up-right to (70, 20)
    auto sCol = std::make_unique<Stroke>();
    sCol->addPoint(Point(50.0, 40.0));
    sCol->addPoint(Point(70.0, 20.0));

    // 3. Emitter lead with arrow: slanted from (50, 60) down-right to (70, 80)
    auto sEmi = std::make_unique<Stroke>();
    sEmi->addPoint(Point(50.0, 60.0));
    sEmi->addPoint(Point(70.0, 80.0));
    // Arrowhead wing 1
    sEmi->addPoint(Point(64.0, 78.0));
    sEmi->addPoint(Point(70.0, 80.0));
    // Arrowhead wing 2
    sEmi->addPoint(Point(68.0, 74.0));

    std::vector<Stroke*> candidates = {sBase.get(), sCol.get(), sEmi.get()};
    auto matches = CircuitFeatureClassifier::detectBjtTransistors(candidates);

    ASSERT_EQ(matches.size(), 1);
    EXPECT_EQ(matches[0].baseBar, sBase.get());
    EXPECT_FALSE(matches[0].isPnp); // Arrow pointing away = NPN
}

TEST(CircuitClassifierTest, TestArrowDetection) {
    // Generate an arrow stroke: shaft from left to right, then V-head back at the tip
    auto stroke = std::make_unique<Stroke>();
    stroke->addPoint(Point(10.0, 50.0));
    stroke->addPoint(Point(30.0, 50.0));
    stroke->addPoint(Point(50.0, 50.0));
    stroke->addPoint(Point(70.0, 50.0)); // tip

    // Arrowhead wing 1
    stroke->addPoint(Point(60.0, 42.0));
    stroke->addPoint(Point(70.0, 50.0)); // back to tip
    // Arrowhead wing 2
    stroke->addPoint(Point(60.0, 58.0));

    Point shaftStart, tip;
    EXPECT_TRUE(CircuitFeatureClassifier::detectArrow(stroke.get(), shaftStart, tip));
    EXPECT_NEAR(shaftStart.x, 10.0, 1.0);
    EXPECT_NEAR(shaftStart.y, 50.0, 1.0);
    EXPECT_NEAR(tip.x, 70.0, 1.0);
    EXPECT_NEAR(tip.y, 50.0, 1.0);

    auto featClass = CircuitFeatureClassifier::classify(stroke.get());
    EXPECT_EQ(featClass, CircuitFeatureClass::Arrow);
}

TEST(CircuitClassifierTest, TestSingleStrokeGroundDetection) {
    // Generate a single-stroke ground: down from top center, then horizontal sweep across bottom
    auto stroke = std::make_unique<Stroke>();
    stroke->addPoint(Point(50.0, 10.0)); // top center
    stroke->addPoint(Point(50.0, 30.0));
    stroke->addPoint(Point(50.0, 45.0)); // bottom of stem
    stroke->addPoint(Point(35.0, 45.0)); // sweep left
    stroke->addPoint(Point(65.0, 45.0)); // sweep right

    Point topPt, botPt;
    EXPECT_TRUE(CircuitFeatureClassifier::detectSingleStrokeGround(stroke.get(), topPt, botPt));
    EXPECT_NEAR(topPt.x, 50.0, 2.0);
    EXPECT_NEAR(topPt.y, 10.0, 2.0);

    auto featClass = CircuitFeatureClassifier::classify(stroke.get());
    EXPECT_EQ(featClass, CircuitFeatureClass::Ground);
}
