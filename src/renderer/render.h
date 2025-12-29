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
