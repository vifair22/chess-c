#pragma once

#include <SDL.h>

typedef struct {
    // Scene/background
    SDL_Color background;

    // Board squares and overlays
    SDL_Color board_light;
    SDL_Color board_dark;
    SDL_Color select_fill;
    SDL_Color select_edge;
    SDL_Color legal_fill;
    SDL_Color hover_fill;
    SDL_Color blocked_fill;
    SDL_Color highlight_fill;
    SDL_Color check_fill;

    // Pieces and text
    SDL_Color piece_light;
    SDL_Color piece_dark;
    SDL_Color hud_text;
    SDL_Color label_text;

    // Panels/menus
    SDL_Color panel_backdrop;
    SDL_Color panel_fill;
    SDL_Color panel_border;
    SDL_Color panel_text;
} RenderPalette;

extern const RenderPalette kPalette;
