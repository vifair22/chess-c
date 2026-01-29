#pragma once

#include "renderer/render.h"
#include "renderer/ui_draw.h"  // Drawing functions moved here
bool ui_handle_main_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h, bool *out_request_quit);
bool ui_handle_pause_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h);
bool ui_handle_win_screen_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h);
int ui_handle_save_modal_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h);
int ui_handle_load_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h, int *out_index);
int ui_handle_load_confirm_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h);
