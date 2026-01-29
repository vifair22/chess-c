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
} RenderSurface;

typedef struct {
    SDL_Texture *piece_textures[16]; // chess pieces + checkers variants
} RenderAssets;

typedef struct {
    bool active;
    char piece;
    int from_file;
    int from_rank;
    int to_file;
    int to_rank;
    Uint32 start_ms;
    Uint32 duration_ms;
    SDL_FPoint from_px;
    SDL_FPoint to_px;
} RenderAnim;

typedef struct {
    // Overlay fade state (0..1)
    float legal_alpha[8][8];
    float highlight_alpha[8][8];
    float blocked_alpha[8][8];
    float hover_alpha[8][8];
    float check_alpha[8][8];
} RenderOverlays;

typedef struct {
    RenderSurface surface;
    RenderAssets assets;
    RenderAnim anim;
    RenderOverlays overlays;
    GameState snapshot;
    int last_turn_number;
} RenderContext;

typedef struct {
    SDL_Rect panel_box;
    int text_scale;
    int list_scale;
    int gap;
    int line_h;
    int list_line_h;
    int list_start_y;
    int title_y;
    int label_x;
    int white_label_y;
    int black_label_y;
    bool visible;
    SDL_Rect white_buttons[4];
    SDL_Rect black_buttons[4];
} RenderPromoLayout;

typedef struct {
    int offset_x;
    int offset_y;
    int cell;
    int board_size;
    int capture_margin;
    int turn_margin;
} BoardGeometry;

bool render_init(RenderContext *ctx, const char *title, int width, int height);
void render_frame(RenderContext *ctx, GameState *state);
BoardGeometry render_compute_board_geometry(const RenderContext *ctx);
bool render_get_promo_layout(const RenderContext *ctx, const GameState *state, RenderPromoLayout *out);
void render_shutdown(RenderContext *ctx);
