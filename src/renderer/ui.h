#pragma once

#include "renderer/render.h"

void ui_draw_main_menu(RenderContext *ctx, const GameState *state);
void ui_draw_pause_menu(RenderContext *ctx, const GameState *state);
void ui_draw_win_screen(RenderContext *ctx, const GameState *state);
