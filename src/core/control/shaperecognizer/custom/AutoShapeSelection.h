/*
 * Xournal++
 *
 * Auto Shape Selection Tool
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>

class Control;

namespace xoj::circuit {

class AutoShapeSelection {
public:
    /**
     * Auto-shape all applicable strokes within the currently active EditSelection.
     * Returns true if any stroke was recognized and transformed.
     */
    static auto autoShapeSelectedContent(Control* control) -> bool;
};

}  // namespace xoj::circuit
