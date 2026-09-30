/*
 * Xournal++
 *
 * Unit tests for RecognizerUndoAction and stroke insertion undo/redo
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/InsertUndoAction.h"
#include "undo/RecognizerUndoAction.h"

TEST(RecognizerUndoActionTest, testRevertRecognitionThenUndoStroke) {
    PageRef page = std::make_shared<XojPage>(100.0, 100.0);
    Layer* layer = page->getSelectedLayer();
    ASSERT_NE(layer, nullptr);

    auto originalStroke = std::make_unique<Stroke>();
    originalStroke->addPoint(Point(10.0, 10.0));
    originalStroke->addPoint(Point(20.0, 20.0));
    Element* originalPtr = originalStroke.get();

    auto recognizedStroke = std::make_unique<Stroke>();
    recognizedStroke->addPoint(Point(10.0, 10.0));
    recognizedStroke->addPoint(Point(20.0, 20.0));
    Element* recognizedPtr = recognizedStroke.get();

    // 1. User draws, stroke recognition occurs:
    // InsertUndoAction tracks the drawn stroke, RecognizerUndoAction tracks the shape replacement
    InsertUndoAction insertAction(page, layer, originalPtr);
    RecognizerUndoAction recognizerAction(page, layer, std::move(originalStroke), recognizedPtr);

    // Document layer receives the recognized shape
    layer->addElement(std::move(recognizedStroke));

    ASSERT_EQ(layer->getElements().size(), 1UL);
    EXPECT_EQ(layer->getElements()[0].get(), recognizedPtr);

    // 2. First Ctrl+Z: Revert recognition back to original stroke
    EXPECT_TRUE(recognizerAction.undo(nullptr));
    ASSERT_EQ(layer->getElements().size(), 1UL);
    EXPECT_EQ(layer->getElements()[0].get(), originalPtr);

    // 3. Second Ctrl+Z: Undo original hand-drawn stroke itself
    EXPECT_TRUE(insertAction.undo(nullptr));
    EXPECT_EQ(layer->getElements().size(), 0UL);

    // 4. First Ctrl+Y: Redo original hand-drawn stroke
    EXPECT_TRUE(insertAction.redo(nullptr));
    ASSERT_EQ(layer->getElements().size(), 1UL);
    EXPECT_EQ(layer->getElements()[0].get(), originalPtr);

    // 5. Second Ctrl+Y: Redo shape recognition
    EXPECT_TRUE(recognizerAction.redo(nullptr));
    ASSERT_EQ(layer->getElements().size(), 1UL);
    EXPECT_EQ(layer->getElements()[0].get(), recognizedPtr);

    // 6. Undo both again
    EXPECT_TRUE(recognizerAction.undo(nullptr));
    ASSERT_EQ(layer->getElements().size(), 1UL);
    EXPECT_EQ(layer->getElements()[0].get(), originalPtr);

    EXPECT_TRUE(insertAction.undo(nullptr));
    EXPECT_EQ(layer->getElements().size(), 0UL);
}

TEST(RecognizerUndoActionTest, testLayerPositionPreservationOnRedo) {
    PageRef page = std::make_shared<XojPage>(100.0, 100.0);
    Layer* layer = page->getSelectedLayer();
    ASSERT_NE(layer, nullptr);

    // Add element 0
    auto elem0 = std::make_unique<Stroke>();
    elem0->addPoint(Point(0.0, 0.0));
    Element* elem0Ptr = elem0.get();
    layer->addElement(std::move(elem0));

    // Recognized stroke at index 1
    auto originalStroke = std::make_unique<Stroke>();
    originalStroke->addPoint(Point(10.0, 10.0));
    Element* originalPtr = originalStroke.get();

    auto recognizedStroke = std::make_unique<Stroke>();
    recognizedStroke->addPoint(Point(10.0, 10.0));
    Element* recognizedPtr = recognizedStroke.get();

    RecognizerUndoAction recognizerAction(page, layer, std::move(originalStroke), recognizedPtr);
    layer->addElement(std::move(recognizedStroke));

    // Add element 2
    auto elem2 = std::make_unique<Stroke>();
    elem2->addPoint(Point(50.0, 50.0));
    Element* elem2Ptr = elem2.get();
    layer->addElement(std::move(elem2));

    ASSERT_EQ(layer->getElements().size(), 3UL);
    EXPECT_EQ(layer->getElements()[0].get(), elem0Ptr);
    EXPECT_EQ(layer->getElements()[1].get(), recognizedPtr);
    EXPECT_EQ(layer->getElements()[2].get(), elem2Ptr);

    // Undo: original stroke should be at index 1
    EXPECT_TRUE(recognizerAction.undo(nullptr));
    ASSERT_EQ(layer->getElements().size(), 3UL);
    EXPECT_EQ(layer->getElements()[0].get(), elem0Ptr);
    EXPECT_EQ(layer->getElements()[1].get(), originalPtr);
    EXPECT_EQ(layer->getElements()[2].get(), elem2Ptr);

    // Redo: recognized stroke MUST return to index 1, not index 0!
    EXPECT_TRUE(recognizerAction.redo(nullptr));
    ASSERT_EQ(layer->getElements().size(), 3UL);
    EXPECT_EQ(layer->getElements()[0].get(), elem0Ptr);
    EXPECT_EQ(layer->getElements()[1].get(), recognizedPtr);
    EXPECT_EQ(layer->getElements()[2].get(), elem2Ptr);
}
