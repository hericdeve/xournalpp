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

#include "control/shaperecognizer/custom/CircuitDecomposer.h"
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

TEST(CircuitClassifierTest, TestParallelCapacitorDetection) {
    // 2 parallel vertical strokes close to each other
    auto plate1 = std::make_unique<Stroke>();
    plate1->addPoint(Point(100.0, 50.0));
    plate1->addPoint(Point(100.0, 80.0));

    auto plate2 = std::make_unique<Stroke>();
    plate2->addPoint(Point(110.0, 50.0));
    plate2->addPoint(Point(110.0, 80.0));

    std::vector<Stroke*> candidates = {plate1.get(), plate2.get()};
    auto matches = CircuitFeatureClassifier::detectParallelCapacitors(candidates);

    ASSERT_EQ(matches.size(), 1);
    EXPECT_NEAR(matches[0].terminalA.x, 86.0, 2.0);
    EXPECT_NEAR(matches[0].terminalB.x, 124.0, 2.0);
    EXPECT_NEAR(matches[0].terminalA.y, 65.0, 2.0);
    EXPECT_NEAR(matches[0].terminalB.y, 65.0, 2.0);
}

TEST(CircuitClassifierTest, TestTextBlockClustering) {
    // 3 strokes forming "22k" close to each other
    auto s1 = std::make_unique<Stroke>();
    s1->addPoint(Point(10.0, 10.0));
    s1->addPoint(Point(18.0, 10.0));
    s1->addPoint(Point(10.0, 20.0));
    s1->addPoint(Point(18.0, 20.0));

    auto s2 = std::make_unique<Stroke>();
    s2->addPoint(Point(22.0, 10.0));
    s2->addPoint(Point(30.0, 10.0));
    s2->addPoint(Point(22.0, 20.0));
    s2->addPoint(Point(30.0, 20.0));

    auto s3 = std::make_unique<Stroke>();
    s3->addPoint(Point(34.0, 8.0));
    s3->addPoint(Point(34.0, 20.0));
    s3->addPoint(Point(40.0, 14.0));

    // And a long wire far away
    auto wire = std::make_unique<Stroke>();
    wire->addPoint(Point(10.0, 100.0));
    wire->addPoint(Point(150.0, 100.0));

    std::vector<Stroke*> candidates = {s1.get(), s2.get(), s3.get(), wire.get()};
    auto protectedText = CircuitDecomposer::clusterTextBlocks(candidates);

    EXPECT_TRUE(protectedText.count(s1.get()) > 0);
    EXPECT_TRUE(protectedText.count(s2.get()) > 0);
    EXPECT_TRUE(protectedText.count(s3.get()) > 0);
    EXPECT_FALSE(protectedText.count(wire.get()) > 0);
}

TEST(CircuitClassifierTest, TestNodeMarkerDetectionAndDiscrimination) {
    // 1. Text cluster with digits and a comma: "1,5"
    // '1' stroke
    auto sDigit1 = std::make_unique<Stroke>();
    sDigit1->addPoint(Point(10.0, 10.0));
    sDigit1->addPoint(Point(10.0, 25.0));

    // comma ',' stroke near '1'
    auto sComma = std::make_unique<Stroke>();
    sComma->addPoint(Point(14.0, 24.0));
    sComma->addPoint(Point(13.0, 27.0));

    // '5' stroke near ','
    auto sDigit5 = std::make_unique<Stroke>();
    sDigit5->addPoint(Point(18.0, 10.0));
    sDigit5->addPoint(Point(18.0, 25.0));

    // 2. Wire with a terminal port circle at the top
    auto sWire = std::make_unique<Stroke>();
    sWire->addPoint(Point(100.0, 30.0));
    sWire->addPoint(Point(100.0, 120.0));

    // Small circular ring at (100, 26) representing open terminal
    auto sPort = std::make_unique<Stroke>();
    for (int i = 0; i <= 16; ++i) {
        double a = (2.0 * M_PI * i) / 16.0;
        sPort->addPoint(Point(100.0 + 4.5 * std::cos(a), 26.0 + 4.5 * std::sin(a)));
    }

    // 3. Solder dot at intermediate wire junction (100, 75)
    auto sSolderDot = std::make_unique<Stroke>();
    sSolderDot->addPoint(Point(100.0, 75.0));
    sSolderDot->addPoint(Point(101.0, 75.5));
    sSolderDot->addPoint(Point(100.5, 76.0));

    std::vector<Stroke*> allStrokes = {
        sDigit1.get(), sComma.get(), sDigit5.get(),
        sWire.get(), sPort.get(), sSolderDot.get()
    };

    // Step A: Text clustering should group '1', ',', '5'
    auto protectedText = CircuitDecomposer::clusterTextBlocks(allStrokes);
    EXPECT_TRUE(protectedText.count(sDigit1.get()) > 0);
    EXPECT_TRUE(protectedText.count(sComma.get()) > 0);
    EXPECT_TRUE(protectedText.count(sDigit5.get()) > 0);
    EXPECT_FALSE(protectedText.count(sPort.get()) > 0);
    EXPECT_FALSE(protectedText.count(sSolderDot.get()) > 0);

    // Step B: Node marker detection should identify sPort and sSolderDot, but ignore sComma
    auto markers = CircuitFeatureClassifier::detectNodeMarkers(allStrokes, protectedText);
    EXPECT_EQ(markers.size(), 2u);

    bool foundPort = false;
    bool foundSolder = false;
    for (const auto& m: markers) {
        if (m.type == CircuitNodeMarkerType::OPEN_TERMINAL) {
            foundPort = true;
            EXPECT_NEAR(m.center.x, 100.0, 1.0);
            EXPECT_NEAR(m.center.y, 26.0, 1.0);
        } else if (m.type == CircuitNodeMarkerType::SOLDER_JUNCTION) {
            foundSolder = true;
            EXPECT_NEAR(m.center.x, 100.0, 1.0);
            EXPECT_NEAR(m.center.y, 75.0, 1.5);
        }
    }
    EXPECT_TRUE(foundPort);
    EXPECT_TRUE(foundSolder);
}

TEST(CircuitClassifierTest, TestStrokeSplittingAtJunctionPoints) {
    // A long vertical wire from (100, 20) to (100, 180)
    auto longWire = std::make_unique<Stroke>();
    longWire->addPoint(Point(100.0, 20.0));
    longWire->addPoint(Point(100.0, 100.0));
    longWire->addPoint(Point(100.0, 180.0));

    // A junction dot at (100, 100)
    std::vector<Point> splitPoints = {Point(100.0, 100.0)};

    auto splits = CircuitDecomposer::splitStrokeAtPoints(longWire.get(), splitPoints);
    ASSERT_EQ(splits.size(), 2u);

    // Sub 0: (100, 20) -> (100, 100)
    EXPECT_NEAR(splits[0]->getPoint(0).y, 20.0, 1e-3);
    EXPECT_NEAR(splits[0]->getPoint(splits[0]->getPointCount() - 1).y, 100.0, 1e-3);

    // Sub 1: (100, 100) -> (100, 180)
    EXPECT_NEAR(splits[1]->getPoint(0).y, 100.0, 1e-3);
    EXPECT_NEAR(splits[1]->getPoint(splits[1]->getPointCount() - 1).y, 180.0, 1e-3);
}
