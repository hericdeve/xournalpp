#include <gtest/gtest.h>

#include "control/shaperecognizer/custom/AutoShapeSelection.h"
#include "control/shaperecognizer/custom/CircuitDecomposer.h"
#include "control/shaperecognizer/custom/CircuitFeatureClassifier.h"
#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "model/Point.h"
#include "model/Stroke.h"

namespace xoj::circuit {

TEST(AutoShapeSelectionTest, TestWireAndVerticalResistorSelection) {
    CustomShapeManager mgr;

    // 1. Horizontal wire
    auto wire = std::make_unique<Stroke>();
    wire->addPoint(Point(50.0, 50.0));
    wire->addPoint(Point(150.0, 50.0));

    // 2. Vertical resistor drawn from (100, 50) down to (100, 160)
    auto resistor = std::make_unique<Stroke>();
    resistor->addPoint(Point(100.0, 50.0));
    resistor->addPoint(Point(100.0, 65.0)); // lead 1
    resistor->addPoint(Point(112.0, 75.0)); // peak 1
    resistor->addPoint(Point(88.0, 85.0));  // valley 1
    resistor->addPoint(Point(112.0, 95.0)); // peak 2
    resistor->addPoint(Point(88.0, 105.0)); // valley 2
    resistor->addPoint(Point(112.0, 115.0)); // peak 3
    resistor->addPoint(Point(88.0, 125.0)); // valley 3
    resistor->addPoint(Point(100.0, 135.0));
    resistor->addPoint(Point(100.0, 160.0)); // lead 2

    // Direct whole-stroke recognition (identical to Draw and Hold):
    CircuitRecognitionResult res;
    auto compStrokes = mgr.recognizeComposite(resistor.get(), &res, 0.60);
    ASSERT_EQ(compStrokes.size(), 1);
    EXPECT_NE(res.matchedTemplate, nullptr);
    EXPECT_EQ(res.matchedTemplate->getId(), "resistor_ieee");

    auto vparts = CircuitDecomposer::decomposeCompoundStroke(resistor.get());
    std::cout << "VERTICAL RESISTOR DECOMPOSE PARTS: " << vparts.size() << std::endl;

    // Snapped stroke points
    const auto& pts = compStrokes[0]->getPointVector();
    std::cout << "Snapped resistor point count: " << pts.size() << std::endl;
    std::cout << "Start: (" << pts.front().x << ", " << pts.front().y << ")" << std::endl;
    std::cout << "End:   (" << pts.back().x << ", " << pts.back().y << ")" << std::endl;

    EXPECT_NEAR(pts.front().x, 100.0, 1.0);
    EXPECT_NEAR(pts.front().y, 50.0, 1.0);
    EXPECT_NEAR(pts.back().x, 100.0, 1.0);
    EXPECT_NEAR(pts.back().y, 160.0, 1.0);
}

TEST(AutoShapeSelectionTest, TestHorizontalResistorDecompositionDebug) {
    CustomShapeManager mgr;

    auto resistor = std::make_unique<Stroke>();
    resistor->addPoint(Point(50.0, 100.0));
    resistor->addPoint(Point(65.0, 100.0)); // lead 1
    resistor->addPoint(Point(75.0, 112.0)); // peak 1
    resistor->addPoint(Point(85.0, 88.0));  // valley 1
    resistor->addPoint(Point(95.0, 112.0)); // peak 2
    resistor->addPoint(Point(105.0, 88.0)); // valley 2
    resistor->addPoint(Point(115.0, 112.0)); // peak 3
    resistor->addPoint(Point(125.0, 88.0)); // valley 3
    resistor->addPoint(Point(135.0, 100.0));
    resistor->addPoint(Point(160.0, 100.0)); // lead 2

    // 1. Direct recognize (Draw and Hold)
    CircuitRecognitionResult dres;
    auto dRec = mgr.recognize(resistor.get(), &dres);
    std::cout << "DRAW AND HOLD DIRECT RECOGNIZE: recognized=" << (dRec != nullptr)
              << " tpl=" << (dres.matchedTemplate ? dres.matchedTemplate->getId() : "none")
              << " score=" << dres.score << std::endl;
    if (dRec) {
        const auto& dpts = dRec->getPointVector();
        std::cout << "  Draw&Hold pts=" << dpts.size()
                  << " start=(" << dpts.front().x << "," << dpts.front().y << ")"
                  << " end=(" << dpts.back().x << "," << dpts.back().y << ")" << std::endl;
        for (size_t p = 0; p < dpts.size(); ++p) {
            std::cout << "    p[" << p << "] = (" << dpts[p].x << ", " << dpts[p].y << ")" << std::endl;
        }
    }

    // 2. Decompose (Ctrl+Alt+K)
    auto parts = CircuitDecomposer::decomposeCompoundStroke(resistor.get());
    std::cout << "DECOMPOSED PARTS COUNT: " << parts.size() << std::endl;
    for (size_t i = 0; i < parts.size(); ++i) {
        auto& part = parts[i];
        auto bbox = part.stroke->getBoundingBox();
        std::cout << "  Part " << i << ": isBody=" << part.isResistorBody
                  << " isLead=" << part.isWireLead
                  << " bbox: (" << bbox.x << ", " << bbox.y
                  << ", w=" << bbox.width << ", h=" << bbox.height << ")"
                  << std::endl;
        if (part.isResistorBody) {
            CircuitRecognitionResult res;
            auto compStrokes = mgr.recognizeComposite(part.stroke.get(), &res, 0.50);
            std::cout << "    recognizeComposite: size=" << compStrokes.size()
                      << " tpl=" << (res.matchedTemplate ? res.matchedTemplate->getId() : "none")
                      << " score=" << res.score << std::endl;
            if (!compStrokes.empty()) {
                const auto& cpts = compStrokes[0]->getPointVector();
                std::cout << "    body snapped pts=" << cpts.size()
                          << " start=(" << cpts.front().x << "," << cpts.front().y << ")"
                          << " end=(" << cpts.back().x << "," << cpts.back().y << ")" << std::endl;
            }
        }
    }

    EXPECT_TRUE(dRec != nullptr);
    ASSERT_NE(dres.matchedTemplate, nullptr);
    EXPECT_EQ(dres.matchedTemplate->getId(), "resistor_ieee");
    EXPECT_NEAR(dRec->getPoint(0).x, 50.0, 1.0);
    EXPECT_NEAR(dRec->getPoint(0).y, 100.0, 1.0);
    EXPECT_NEAR(dRec->getPoint(dRec->getPointCount() - 1).x, 160.0, 1.0);
    EXPECT_NEAR(dRec->getPoint(dRec->getPointCount() - 1).y, 100.0, 1.0);
}




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

TEST(AutoShapeSelectionTest, TestResistorWithTerminalDotsRecognition) {
    CustomShapeManager mgr;
    mgr.loadDefaults();

    // 1. Top terminal dot at (100, 48)
    auto dotTop = std::make_unique<Stroke>();
    dotTop->addPoint(Point(99.0, 47.0));
    dotTop->addPoint(Point(101.0, 49.0));

    // 2. Resistor stroke drawn from (100, 50) down to (100, 160)
    auto resistor = std::make_unique<Stroke>();
    resistor->addPoint(Point(100.0, 50.0));
    resistor->addPoint(Point(100.0, 65.0)); // lead 1
    resistor->addPoint(Point(112.0, 75.0)); // peak 1
    resistor->addPoint(Point(88.0, 85.0));  // valley 1
    resistor->addPoint(Point(112.0, 95.0)); // peak 2
    resistor->addPoint(Point(88.0, 105.0)); // valley 2
    resistor->addPoint(Point(112.0, 115.0)); // peak 3
    resistor->addPoint(Point(88.0, 125.0)); // valley 3
    resistor->addPoint(Point(100.0, 135.0));
    resistor->addPoint(Point(100.0, 160.0)); // lead 2

    // 3. Bottom terminal dot at (100, 162)
    auto dotBot = std::make_unique<Stroke>();
    dotBot->addPoint(Point(99.0, 161.0));
    dotBot->addPoint(Point(101.0, 163.0));

    std::vector<Stroke*> candidates = {dotTop.get(), resistor.get(), dotBot.get()};

    // 1. Node markers detection
    auto markers = CircuitFeatureClassifier::detectNodeMarkers(candidates, {});
    ASSERT_EQ(markers.size(), 2);
    EXPECT_NEAR(markers[0].center.x, 100.0, 2.0);
    EXPECT_NEAR(markers[0].center.y, 48.0, 2.0);
    EXPECT_NEAR(markers[1].center.x, 100.0, 2.0);
    EXPECT_NEAR(markers[1].center.y, 162.0, 2.0);

    // 2. Component recognition
    CircuitRecognitionResult resCheck = CircuitRecognizer::recognize(resistor.get(), mgr.getTemplates(), 0.60);
    EXPECT_TRUE(resCheck.matched);
    ASSERT_NE(resCheck.matchedTemplate, nullptr);
    EXPECT_EQ(resCheck.matchedTemplate->getId(), "resistor_ieee");

    // 3. Clamping to markers
    Point termStart = resCheck.terminalStart;
    Point termEnd = resCheck.terminalEnd;
    for (const auto& marker: markers) {
        if (termStart.lineLengthTo(marker.center) <= 24.0) termStart = marker.center;
        if (termEnd.lineLengthTo(marker.center) <= 24.0) termEnd = marker.center;
    }

    EXPECT_NEAR(termStart.x, 100.0, 1.0);
    EXPECT_NEAR(termStart.y, 48.0, 1.0);
    EXPECT_NEAR(termEnd.x, 100.0, 1.0);
    EXPECT_NEAR(termEnd.y, 162.0, 1.0);

    // 4. Snapping
    auto compStrokes = mgr.snapShapeComposite(resCheck.matchedTemplate, termStart, termEnd, resistor.get(), true, nullptr,
                                             resCheck.bodyStartRatio, resCheck.bodyEndRatio);
    ASSERT_EQ(compStrokes.size(), 1);
    const auto& pts = compStrokes[0]->getPointVector();
    EXPECT_NEAR(pts.front().x, 100.0, 1.0);
    EXPECT_NEAR(pts.front().y, 48.0, 1.0);
    EXPECT_NEAR(pts.back().x, 100.0, 1.0);
    EXPECT_NEAR(pts.back().y, 162.0, 1.0);
}

}  // namespace xoj::circuit
