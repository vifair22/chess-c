#pragma once

#include <SDL.h>
#include <stdbool.h>

#include "game/logic.h"
#include "renderer/palette.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int window_width;
    int window_height;
    SDL_Texture *piece_textures[12]; // chess pieces: white/black pawn, knight, bishop, rook, queen, king
    // Animation state
    struct {
        bool active;
        char piece;
        int from_file, from_rank;
        int to_file, to_rank;
        Uint32 start_ms;
        Uint32 duration_ms;
        SDL_FPoint from_px;
        SDL_FPoint to_px;
    } anim;
    int last_turn_number;
    // Overlay fade state (0..1)
    float legal_alpha[8][8];
    float highlight_alpha[8][8];
    float blocked_alpha[8][8];
    float hover_alpha[8][8];
    float check_alpha[8][8];
} RenderContext;

typedef struct {
    int offset_x;
    int offset_y;
    int cell;
    int board_size;
} BoardGeometry;

bool render_init(RenderContext *ctx, const char *title, int width, int height);
void render_frame(RenderContext *ctx, GameState *state);
BoardGeometry render_compute_board_geometry(const RenderContext *ctx);
void render_shutdown(RenderContext *ctx);
