#include "renderer/palette.h"

// Shared palette for renderer/UI elements.
const RenderPalette kPalette = {
    .background = {12, 12, 16, 255},

    .board_light = {230, 230, 230, 255},
    .board_dark = {26, 26, 26, 255},
    .select_fill = {200, 180, 80, 120},
    .select_edge = {180, 150, 60, 255},
    .legal_fill = {90, 180, 120, 120},
    .hover_fill = {120, 160, 220, 100},
    .blocked_fill = {200, 90, 90, 70},
    .highlight_fill = {200, 200, 60, 120},
    .check_fill = {200, 60, 60, 140},

    .piece_light = {245, 245, 245, 255},
    .piece_dark = {30, 30, 30, 255},
    .hud_text = {220, 220, 220, 230},
    .label_text = {220, 220, 220, 230},

    .panel_backdrop = {10, 12, 16, 180},
    .panel_fill = {28, 32, 40, 240},
    .panel_border = {90, 120, 180, 255},
    .panel_text = {230, 230, 230, 255},
};
