/*
 * Xournal++
 *
 * Pen Preset data structure
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include "control/ToolEnums.h"
#include "model/LineStyle.h"
#include "util/Color.h"

struct PenPreset {
    std::string id;
    std::string name;
    ToolType toolType = TOOL_PEN;
    ToolSize size = TOOL_SIZE_FINE;
    Color color = Color(0x000000);  // black
    LineStyle lineStyle = LineStyle();
    DrawingType drawingType = DRAWING_TYPE_DEFAULT;
    bool fillEnabled = false;
    int fillAlpha = 128;

    PenPreset() = default;

    PenPreset(std::string id, std::string name, ToolType toolType, ToolSize size, Color color,
              LineStyle lineStyle = LineStyle(), DrawingType drawingType = DRAWING_TYPE_DEFAULT,
              bool fillEnabled = false, int fillAlpha = 128):
            id(std::move(id)),
            name(std::move(name)),
            toolType(toolType),
            size(size),
            color(color),
            lineStyle(lineStyle),
            drawingType(drawingType),
            fillEnabled(fillEnabled),
            fillAlpha(fillAlpha) {}

    bool operator==(const PenPreset& other) const {
        return id == other.id && name == other.name && toolType == other.toolType && size == other.size &&
               color == other.color && lineStyle == other.lineStyle && drawingType == other.drawingType &&
               fillEnabled == other.fillEnabled && fillAlpha == other.fillAlpha;
    }

    bool operator!=(const PenPreset& other) const { return !(*this == other); }
};
