/*
 * Xournal++
 *
 * Tests for Image-Based Snip / Topological Circuit Recognizer
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/shaperecognizer/custom/CustomShapeManager.h"
#include "control/shaperecognizer/image/BinaryGrid.h"
#include "control/shaperecognizer/image/CircuitGraph.h"
#include "control/shaperecognizer/image/CircuitSnipRecognizer.h"
#include "control/shaperecognizer/image/ZhangSuenThinner.h"
#include "model/Stroke.h"

using namespace xoj::circuit;

TEST(CircuitSnipTest, TestBinaryGridAndZhangSuenThinning) {
    BinaryGrid grid(50, 50);

    // Draw a thick horizontal bar (width 30, height 5)
    for (int y = 20; y <= 24; ++y) {
        for (int x = 10; x <= 40; ++x) {
            grid.set(x, y, 1);
        }
    }

    auto blobs = grid.findConnectedBlobs();
    ASSERT_EQ(blobs.size(), 1);
    EXPECT_EQ(blobs[0].width(), 31);
    EXPECT_EQ(blobs[0].height(), 5);

    int iterations = ZhangSuenThinner::thin(grid);
    EXPECT_GT(iterations, 0);

    int totalRemaining = 0;
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 50; ++x) {
            if (grid.get(x, y) > 0) {
                totalRemaining++;
            }
        }
    }
    EXPECT_GT(totalRemaining, 0);
}

TEST(CircuitSnipTest, TestCircuitGraphTopologicalExtraction) {
    BinaryGrid grid(60, 60);

    // Draw a T-junction: horizontal line at y=30 from x=10 to x=50,
    // and vertical line at x=30 from y=30 to y=55.
    for (int x = 10; x <= 50; ++x) {
        grid.set(x, 30, 1);
    }
    for (int y = 30; y <= 55; ++y) {
        grid.set(30, y, 1);
    }

    CircuitGraph graph = CircuitGraph::extractFromSkeleton(grid);

    // Should have 1 junction node (degree >= 3) near (30, 30)
    // and 3 endpoints (degree == 1) near (10, 30), (50, 30), and (30, 55).
    const auto& nodes = graph.getNodes();
    ASSERT_GE(nodes.size(), 3);

    int junctionCount = 0;
    int endpointCount = 0;
    for (const auto& n: nodes) {
        if (n.degree >= 3) {
            junctionCount++;
            EXPECT_NEAR(n.pos.x, 30.0, 2.0);
            EXPECT_NEAR(n.pos.y, 30.0, 2.0);
        } else if (n.degree == 1) {
            endpointCount++;
        }
    }

    EXPECT_GE(junctionCount, 1);
    EXPECT_GE(endpointCount, 2);
}

TEST(CircuitSnipTest, TestCircuitSnipProcessWithResistorAndText) {
    CustomShapeManager mgr;
    mgr.loadDefaults();

    std::vector<Stroke*> selectedStrokes;

    // 1. Text stroke "2" (small dimensions, high sinuosity)
    auto textStroke = std::make_unique<Stroke>();
    textStroke->addPoint(Point(150.0, 150.0));
    textStroke->addPoint(Point(155.0, 145.0));
    textStroke->addPoint(Point(160.0, 150.0));
    textStroke->addPoint(Point(152.0, 158.0));
    textStroke->addPoint(Point(162.0, 158.0));
    selectedStrokes.push_back(textStroke.get());

    // 2. Wire stroke 1
    auto wire1 = std::make_unique<Stroke>();
    wire1->addPoint(Point(50.0, 100.0));
    wire1->addPoint(Point(100.0, 100.0));
    selectedStrokes.push_back(wire1.get());

    // 3. Resistor zig-zag stroke
    auto resistor = std::make_unique<Stroke>();
    resistor->addPoint(Point(100.0, 100.0));
    resistor->addPoint(Point(110.0, 80.0));
    resistor->addPoint(Point(120.0, 120.0));
    resistor->addPoint(Point(130.0, 80.0));
    resistor->addPoint(Point(140.0, 120.0));
    resistor->addPoint(Point(150.0, 80.0));
    resistor->addPoint(Point(160.0, 120.0));
    resistor->addPoint(Point(170.0, 100.0));
    selectedStrokes.push_back(resistor.get());

    // 4. Wire stroke 2
    auto wire2 = std::make_unique<Stroke>();
    wire2->addPoint(Point(170.0, 100.0));
    wire2->addPoint(Point(220.0, 100.0));
    selectedStrokes.push_back(wire2.get());

    auto result = CircuitSnipRecognizer::processSnip(selectedStrokes, &mgr);

    // Text stroke should be shielded
    EXPECT_FALSE(result.protectedTextStrokes.empty());
    bool foundProtectedText = false;
    for (auto* s: result.protectedTextStrokes) {
        if (s == textStroke.get()) {
            foundProtectedText = true;
            break;
        }
    }
    EXPECT_TRUE(foundProtectedText);
}

TEST(CircuitSnipTest, TestResistorWithTerminalDots) {
    CustomShapeManager mgr;
    mgr.loadDefaults();

    auto dot1 = std::make_unique<Stroke>();
    dot1->addPoint(Point(100.0, 48.0));
    dot1->addPoint(Point(100.0, 50.0));

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

    auto dot2 = std::make_unique<Stroke>();
    dot2->addPoint(Point(100.0, 160.0));
    dot2->addPoint(Point(100.0, 162.0));

    std::vector<Stroke*> selected = {dot1.get(), resistor.get(), dot2.get()};
    auto result = CircuitSnipRecognizer::processSnip(selected, &mgr);
    std::cout << "TEST RESISTOR WITH DOTS: snip success=" << result.success 
              << " strokesToInsert=" << result.strokesToInsert.size() << std::endl;
    for (size_t i = 0; i < result.strokesToInsert.size(); ++i) {
        std::cout << "  Stroke " << i << " pts=" << result.strokesToInsert[i]->getPointCount()
                  << " start=(" << result.strokesToInsert[i]->getPoint(0).x << "," << result.strokesToInsert[i]->getPoint(0).y << ")"
                  << " end=(" << result.strokesToInsert[i]->getPoint(result.strokesToInsert[i]->getPointCount()-1).x << ","
                  << result.strokesToInsert[i]->getPoint(result.strokesToInsert[i]->getPointCount()-1).y << ")" << std::endl;
    }
}

