// UI click handlers - dispatches user input to game state
// Drawing functions are in ui_draw.c, layout in ui_layout.c, buttons in ui_button.c

#include "renderer/ui.h"
#include "renderer/ui_layout.h"
#include "renderer/ui_button.h"
#include "renderer/ui_draw.h"
#include "save/replay.h"

#include <SDL.h>
#include <string.h>

bool ui_handle_main_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h, bool *out_request_quit) {
    if (!state) return false;
    if (out_request_quit) {
        *out_request_quit = false;
    }
    if (state->ui.show_new_menu) {
        MenuLayout layout = menu_layout(window_w, window_h);
        NewGameButtons buttons = new_game_buttons(&layout);
        if (point_in_rect(mouse_x, mouse_y, buttons.chess)) {
            game_reset(state, "chess");
            state->ui.new_menu_index = 0;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons.checkers)) {
            game_reset(state, "checkers");
            state->ui.new_menu_index = 1;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons.ai_side)) {
            if (state->ai.side == AI_SIDE_NONE) {
                state->ai.side = AI_SIDE_WHITE;
            } else if (state->ai.side == AI_SIDE_WHITE) {
                state->ai.side = AI_SIDE_BLACK;
            } else {
                state->ai.side = AI_SIDE_NONE;
            }
            state->ui.new_menu_index = 2;
            return true;
        }
        for (int i = 0; i < 9; ++i) {
            if (point_in_rect(mouse_x, mouse_y, buttons.depth[i])) {
                state->ai.depth = i + 1;
                state->ui.new_menu_index = 3 + i;
                return true;
            }
        }
        if (point_in_rect(mouse_x, mouse_y, buttons.start)) {
            replay_global_stop();
            replay_global_start(state, SDL_GetTicks());
            state->ui.replay_mode_active = false;
            state->ui.show_main_menu = false;
            state->ui.show_new_menu = false;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons.back)) {
            state->ui.show_new_menu = false;
            return true;
        }
        return false;
    }

    MainMenuLayout layout = main_menu_layout(window_w, window_h);
    MenuButtons buttons = main_menu_buttons(&layout);
    if (point_in_rect(mouse_x, mouse_y, buttons.new_game)) {
        state->ui.show_new_menu = true;
        state->ui.new_menu_index = 0;
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons.load)) {
        state->ui.show_load_menu = true;
        state->ui.load_needs_refresh = true;
        state->ui.load_is_replay = false;
        state->ui.load_menu_focus = 0;
        state->ui.load_scroll = 0;
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons.replays)) {
        state->ui.show_load_menu = true;
        state->ui.load_needs_refresh = true;
        state->ui.load_is_replay = true;
        state->ui.load_menu_focus = 0;
        state->ui.load_scroll = 0;
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons.quit)) {
        if (out_request_quit) {
            *out_request_quit = true;
        }
        return true;
    }
    return false;
}

bool ui_handle_pause_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h) {
    if (!state) return false;
    MenuLayout layout = menu_layout(window_w, window_h);
    if (mouse_x < layout.panel.x || mouse_x > layout.panel.x + layout.panel.w ||
        mouse_y < layout.panel.y || mouse_y > layout.panel.y + layout.panel.h) {
        return false;
    }
    SDL_Rect buttons[4];
    int y = layout.body_y + layout.line_height * 2;
    if (state->ui.replay_mode_active) {
        menu_vertical_buttons(&layout, buttons, 2, y);
        if (point_in_rect(mouse_x, mouse_y, buttons[0])) {
            state->ui.show_pause_menu = false;
            state->ui.pause_menu_index = 0;
            state->ui.replay_paused = false;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
            game_full_reset(state);
            replay_global_stop();
            state->ui.pause_menu_index = 1;
            return true;
        }
    } else {
        menu_vertical_buttons(&layout, buttons, 4, y);
        if (point_in_rect(mouse_x, mouse_y, buttons[0])) {
            state->ui.show_pause_menu = false;
            state->ui.pause_menu_index = 0;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
            state->ui.show_save_modal = true;
            state->ui.save_len = 0;
            state->ui.save_name[0] = '\0';
            state->ui.save_menu_index = 0;
            state->ui.save_is_replay = false;
            state->ui.pause_menu_index = 1;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons[2])) {
            state->ui.show_load_menu = true;
            state->ui.load_needs_refresh = true;
            state->ui.load_is_replay = false;
            state->ui.load_menu_focus = 0;
            state->ui.load_scroll = 0;
            state->ui.pause_menu_index = 2;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons[3])) {
            game_resign(state);
            state->ui.pause_menu_index = 3;
            return true;
        }
    }
    return false;
}

bool ui_handle_win_screen_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h) {
    if (!state) return false;
    MenuLayout layout = menu_layout(window_w, window_h);
    if (mouse_x < layout.panel.x || mouse_x > layout.panel.x + layout.panel.w ||
        mouse_y < layout.panel.y || mouse_y > layout.panel.y + layout.panel.h) {
        return false;
    }
    SDL_Rect buttons[2];
    int y = layout.body_y + layout.line_height * 5;
    menu_vertical_buttons(&layout, buttons, 2, y);
    if (point_in_rect(mouse_x, mouse_y, buttons[0])) {
        if (state->ui.replay_saved || state->ui.replay_mode_active) {
            return true;
        }
        state->ui.show_save_modal = true;
        state->ui.save_len = 0;
        state->ui.save_name[0] = '\0';
        state->ui.save_menu_index = 0;
        state->ui.save_is_replay = true;
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
        game_full_reset(state);
        replay_global_stop();
        return true;
    }
    return false;
}

int ui_handle_save_modal_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h) {
    if (!state) return 0;
    MenuLayout layout = menu_layout(window_w, window_h);
    if (mouse_x < layout.panel.x || mouse_x > layout.panel.x + layout.panel.w ||
        mouse_y < layout.panel.y || mouse_y > layout.panel.y + layout.panel.h) {
        return 0;
    }
    SDL_Rect buttons[2];
    int y = layout.body_y + layout.line_height + 6;
    SDL_Rect input = {layout.panel.x + 24, y, layout.panel.w - 48, layout.line_height + 10};
    y = input.y + input.h + layout.line_height;
    menu_vertical_buttons(&layout, buttons, 2, y);
    if (point_in_rect(mouse_x, mouse_y, buttons[0])) {
        if (state->ui.save_len > 0) {
            state->ui.save_menu_index = 0;
            return 1; // save
        }
        return 0;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
        state->ui.save_menu_index = 1;
        return -1; // cancel
    }
    return 0;
}

int ui_handle_load_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h, int *out_index) {
    if (!state) return 0;
    if (out_index) *out_index = -1;
    MenuLayout layout = menu_layout(window_w, window_h);
    if (mouse_x < layout.panel.x || mouse_x > layout.panel.x + layout.panel.w ||
        mouse_y < layout.panel.y || mouse_y > layout.panel.y + layout.panel.h) {
        return 0;
    }
    int y = layout.body_y + layout.line_height + 6;
    int count = state->ui.load_count;
    if (count > 0) {
        int max_visible = 10;
        int start = state->ui.load_scroll;
        if (start < 0) start = 0;
        if (start > count - 1) start = count - 1;
        int visible = count - start;
        if (visible > max_visible) visible = max_visible;
        int list_row_h = layout.line_height + 6;
        int list_gap = 12;
        int list_total_h = max_visible * list_row_h + (max_visible - 1) * list_gap;
        SDL_Rect list_box = {layout.panel.x + 24, y, layout.panel.w - 48, list_total_h};
        int inner_pad = 8;
        int inner_x = list_box.x + inner_pad;
        int inner_w = list_box.w - inner_pad * 2;
        if (count > max_visible) {
            int bar_w = 6;
            int bar_pad = 6;
            inner_w -= (bar_w + bar_pad);
        }
        int inner_y = list_box.y + inner_pad;
        int inner_h = list_row_h - 2;
        int inner_gap = list_gap;
        SDL_Rect btn_rects[10];
        button_column(inner_x, inner_y, inner_w, inner_h, inner_gap, btn_rects, visible);
        for (int i = 0; i < visible; ++i) {
            if (point_in_rect(mouse_x, mouse_y, btn_rects[i])) {
                if (out_index) *out_index = start + i;
                state->ui.load_menu_focus = start + i;
                return 1;
            }
        }
        y = list_box.y + list_box.h + layout.line_height;
    } else {
        y += layout.line_height * 2;
    }
    int btn_h = layout.line_height + 6;
    int margin = 24;
    int max_y = layout.panel.y + layout.panel.h - margin - btn_h;
    if (y > max_y) y = max_y;
    SDL_Rect cancel_btn[1];
    menu_vertical_buttons(&layout, cancel_btn, 1, y);
    if (point_in_rect(mouse_x, mouse_y, cancel_btn[0])) {
        state->ui.load_menu_focus = state->ui.load_count;
        return -1;
    }
    return 0;
}

int ui_handle_load_confirm_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h) {
    if (!state) return 0;
    MenuLayout layout = menu_layout(window_w, window_h);
    if (mouse_x < layout.panel.x || mouse_x > layout.panel.x + layout.panel.w ||
        mouse_y < layout.panel.y || mouse_y > layout.panel.y + layout.panel.h) {
        return 0;
    }
    SDL_Rect buttons[2];
    int y = layout.body_y + layout.line_height * 2;
    menu_vertical_buttons(&layout, buttons, 2, y);
    if (point_in_rect(mouse_x, mouse_y, buttons[0])) {
        state->ui.load_confirm_index = 0;
        return 1;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
        state->ui.load_confirm_index = 1;
        return -1;
    }
    return 0;
}
