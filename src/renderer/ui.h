#pragma once

#include "renderer/render.h"

void ui_draw_main_menu(RenderContext *ctx, const GameState *state);
void ui_draw_pause_menu(RenderContext *ctx, const GameState *state);
void ui_draw_win_screen(RenderContext *ctx, const GameState *state);
bool ui_handle_main_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h, bool *out_request_quit);
bool ui_handle_pause_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h);
bool ui_handle_win_screen_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h);
