/*
 * Xournal++
 *
 * Circuit Component Pattern Recognizer
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <vector>

#include "CircuitTemplate.h"
#include "model/Point.h"
#include "model/Stroke.h"

namespace xoj::circuit {

struct CircuitRecognitionResult {
    bool matched{false};
    const CircuitTemplate* matchedTemplate{nullptr};
    Point terminalStart{0.0, 0.0};
    Point terminalEnd{0.0, 0.0};
    double score{0.0};
    bool reversed{false};
    double bodyStartRatio{0.20};
    double bodyEndRatio{0.80};
};

class CircuitRecognizer {
public:
    static auto recognize(const Stroke* stroke, const std::vector<std::shared_ptr<CircuitTemplate>>& templates,
                          double threshold = 0.65) -> CircuitRecognitionResult;
};

}  // namespace xoj::circuit
