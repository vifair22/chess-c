#include "renderer/palette.h"

// Shared palette for renderer/UI elements.
const RenderPalette kPalette = {
    .background = {15, 20, 26, 255},

    .board_light = {255, 102, 255, 255},
    .board_dark = {118, 150, 86, 255},
    .select_fill = {246, 211, 101, 120},
    .select_edge = {209, 139, 71, 255},
    .legal_fill = {120, 200, 140, 120},
    .hover_fill = {120, 160, 220, 100},
    .blocked_fill = {200, 90, 90, 70},
    .highlight_fill = {210, 180, 80, 120},
    .check_fill = {200, 60, 60, 140},

    .piece_light = {240, 240, 240, 255},
    .piece_dark = {40, 40, 40, 255},
    .hud_text = {212, 220, 228, 230},
    .label_text = {212, 220, 228, 230},

    .panel_backdrop = {10, 12, 16, 180},
    .panel_fill = {24, 32, 44, 240},
    .panel_border = {80, 120, 200, 255},
    .panel_text = {220, 230, 240, 255},
};
