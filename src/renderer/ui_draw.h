#ifndef UI_DRAW_H
#define UI_DRAW_H

#include "renderer/render.h"
#include "game/logic.h"

// Menu drawing functions
void ui_draw_main_menu(RenderContext *ctx, const GameState *state);
void ui_draw_pause_menu(RenderContext *ctx, const GameState *state);
void ui_draw_win_screen(RenderContext *ctx, const GameState *state);

// Modal drawing functions (used internally and by ui.c)
void ui_draw_new_menu(RenderContext *ctx, const GameState *state);
void ui_draw_save_modal(RenderContext *ctx, const GameState *state);
void ui_draw_load_menu(RenderContext *ctx, const GameState *state);
void ui_draw_load_confirm(RenderContext *ctx, const GameState *state);

#endif // UI_DRAW_H
