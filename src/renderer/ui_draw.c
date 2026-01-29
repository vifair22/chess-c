#include "renderer/ui_draw.h"
#include "renderer/ui_layout.h"
#include "renderer/ui_button.h"
#include "renderer/draw.h"
#include "renderer/palette.h"

#include <SDL.h>
#include <string.h>
#include <time.h>

static const char *ai_side_label(AiSide side) {
    switch (side) {
        case AI_SIDE_WHITE: return "WHITE";
        case AI_SIDE_BLACK: return "BLACK";
        default: return "OFF";
    }
}

void ui_draw_new_menu(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "NEW GAME", true);
    NewGameButtons buttons = new_game_buttons(&layout);
    bool allow_hover = state->ui.menu_mouse_active;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    int x = layout.body_x;
    int y = layout.body_y;
    draw_text(ctx->surface.renderer, x, y, layout.scale, kPalette.panel_text, "MODE");
    y += layout.line_height;

    bool chess_active = strncmp(state->match.mode, "chess", 5) == 0;
    draw_button(ctx, buttons.chess, "CHESS", chess_active,
        state->ui.menu_keyboard_active && state->ui.new_menu_index == 0, layout.scale, mouse_x, mouse_y, allow_hover);
    draw_button(ctx, buttons.checkers, "CHECKERS", !chess_active,
        state->ui.menu_keyboard_active && state->ui.new_menu_index == 1, layout.scale, mouse_x, mouse_y, allow_hover);

    y = buttons.ai_side.y - layout.line_height;
    draw_text(ctx->surface.renderer, x, y, layout.scale, kPalette.panel_text, "AI SIDE");
    draw_button(ctx, buttons.ai_side, ai_side_label(state->ai.side), false,
        state->ui.menu_keyboard_active && state->ui.new_menu_index == 2, layout.scale, mouse_x, mouse_y, allow_hover);

    y = buttons.depth[0].y - layout.line_height;
    draw_text(ctx->surface.renderer, x, y, layout.scale, kPalette.panel_text, "AI DEPTH");
    for (int i = 0; i < 9; ++i) {
        char label[4] = {0};
        if (i < 8) {
            label[0] = (char)('1' + i);
            label[1] = '\0';
        } else {
            snprintf(label, sizeof(label), "10S");
        }
        bool focused = state->ui.menu_keyboard_active && state->ui.new_menu_index == (3 + i);
        draw_button(ctx, buttons.depth[i], label, state->ai.depth == (i + 1), focused,
            layout.scale, mouse_x, mouse_y, allow_hover);
    }

    draw_button(ctx, buttons.start, "START", false,
        state->ui.menu_keyboard_active && state->ui.new_menu_index == 12, layout.scale, mouse_x, mouse_y, allow_hover);
    draw_button(ctx, buttons.back, "BACK", false,
        state->ui.menu_keyboard_active && state->ui.new_menu_index == 13, layout.scale, mouse_x, mouse_y, allow_hover);

    int hint_y = buttons.back.y + buttons.back.h + layout.line_height;
    draw_text(ctx->surface.renderer, x, hint_y, layout.scale, kPalette.panel_text, "ESC TO GO BACK");
}

void ui_draw_save_modal(RenderContext *ctx, const GameState *state) {
    const char *title = state->ui.save_is_replay ? "SAVE REPLAY" : "SAVE GAME";
    MenuLayout layout = draw_panel_base(ctx, title, true);
    bool allow_hover = state->ui.menu_mouse_active;
    SDL_Rect buttons[2];
    int y = layout.body_y;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    draw_panel_text(ctx, &layout, "ENTER A NAME", y);
    y += layout.line_height + 6;

    SDL_Rect input = {layout.panel.x + 24, y, layout.panel.w - 48, layout.line_height + 10};
    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_fill.r, kPalette.panel_fill.g, kPalette.panel_fill.b, 255);
    SDL_RenderFillRect(ctx->surface.renderer, &input);
    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &input);
    char save_buf[96];
    snprintf(save_buf, sizeof(save_buf), "%s", state->ui.save_name);
    if (((SDL_GetTicks() / 500) % 2) == 0) {
        size_t len = strlen(save_buf);
        if (len + 1 < sizeof(save_buf)) {
            save_buf[len] = '_';
            save_buf[len + 1] = '\0';
        }
    }
    draw_text(ctx->surface.renderer, input.x + 6, input.y + 5, layout.scale, kPalette.panel_text, save_buf);

    y = input.y + input.h + layout.line_height;
    menu_vertical_buttons(&layout, buttons, 2, y);
    bool save_disabled = state->ui.save_len == 0;
    bool save_focused = state->ui.menu_keyboard_active && state->ui.save_menu_index == 0;
    draw_button(ctx, buttons[0], "SAVE", false, save_focused && !save_disabled,
        layout.scale, mouse_x, mouse_y, allow_hover && !save_disabled);
    if (save_disabled) {
        SDL_Color dim = kPalette.panel_fill;
        dim.a = 180;
        SDL_SetRenderDrawColor(ctx->surface.renderer, dim.r, dim.g, dim.b, dim.a);
        SDL_RenderFillRect(ctx->surface.renderer, &buttons[0]);
        SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &buttons[0]);
        int text_w = measure_text_width("SAVE", layout.scale);
        int text_x = buttons[0].x + (buttons[0].w - text_w) / 2;
        int text_y = buttons[0].y + (buttons[0].h - (7 + 1) * layout.scale) / 2;
        draw_text(ctx->surface.renderer, text_x, text_y, layout.scale, kPalette.label_text, "SAVE");
    }
    draw_button(ctx, buttons[1], "CANCEL", false, state->ui.menu_keyboard_active && state->ui.save_menu_index == 1,
        layout.scale, mouse_x, mouse_y, allow_hover);
}

void ui_draw_load_menu(RenderContext *ctx, const GameState *state) {
    const char *title = state->ui.load_is_replay ? "LOAD REPLAY" : "LOAD GAME";
    MenuLayout layout = draw_panel_base(ctx, title, true);
    bool allow_hover = state->ui.menu_mouse_active;
    int y = layout.body_y;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    draw_panel_text(ctx, &layout, state->ui.load_is_replay ? "SELECT A REPLAY" : "SELECT A SAVE", y);
    y += layout.line_height + 6;

    int count = state->ui.load_count;
    if (count <= 0) {
        draw_panel_text(ctx, &layout, state->ui.load_is_replay ? "NO REPLAYS FOUND" : "NO SAVES FOUND", y);
        y += layout.line_height * 2;
    } else {
        int max_visible = 10;
        int start = state->ui.load_scroll;
        if (start < 0) start = 0;
        if (start > count - 1) start = count - 1;
        int visible = count - start;
        if (visible > max_visible) visible = max_visible;
        int list_row_h = layout.line_height + 6;
        int list_gap = 12;
        int bar_w = 6;
        int bar_pad = 6;
        int list_total_h = max_visible * list_row_h + (max_visible - 1) * list_gap;
        SDL_Rect list_box = {layout.panel.x + 24, y, layout.panel.w - 48, list_total_h};
        SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_fill.r, kPalette.panel_fill.g, kPalette.panel_fill.b, 255);
        SDL_RenderFillRect(ctx->surface.renderer, &list_box);
        SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &list_box);

        SDL_Rect btn_rects[10];
        int inner_pad = 8;
        int inner_x = list_box.x + inner_pad;
        int inner_w = list_box.w - inner_pad * 2;
        if (count > max_visible) {
            inner_w -= (bar_w + bar_pad);
        }
        int inner_y = list_box.y + inner_pad;
        int inner_h = list_row_h - 2;
        int inner_gap = list_gap;
        button_column(inner_x, inner_y, inner_w, inner_h, inner_gap, btn_rects, visible);
        for (int i = 0; i < visible; ++i) {
            const char *name = state->ui.load_names[start + i];
            char label[64];
            strncpy(label, name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
            char *dot = strrchr(label, '.');
            if (dot) {
                if (state->ui.load_is_replay && strcmp(dot, ".replay") == 0) {
                    *dot = '\0';
                } else if (!state->ui.load_is_replay && strcmp(dot, ".save") == 0) {
                    *dot = '\0';
                }
            }
            char time_buf[32] = {0};
            struct tm tm_local;
            if (localtime_s(&tm_local, &state->ui.load_times[start + i]) == 0) {
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", &tm_local);
            }
            int focus_index = start + i;
            bool focused = state->ui.menu_keyboard_active && state->ui.load_menu_focus == focus_index;
            draw_button_lr(ctx, btn_rects[i], label, time_buf, false, focused, layout.scale, mouse_x, mouse_y, allow_hover);
        }
        if (count > max_visible) {
            int bar_x = list_box.x + list_box.w - bar_w - 4;
            int bar_y = list_box.y + 4;
            int bar_h = list_box.h - 8;
            if (bar_h < 8) bar_h = 8;
            SDL_Rect track = {bar_x, bar_y, bar_w, bar_h};
            SDL_Color track_col = kPalette.panel_border;
            track_col.a = 120;
            SDL_SetRenderDrawColor(ctx->surface.renderer, track_col.r, track_col.g, track_col.b, track_col.a);
            SDL_RenderFillRect(ctx->surface.renderer, &track);

            int max_start = count - max_visible;
            if (max_start < 1) max_start = 1;
            float t = (float)start / (float)max_start;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            int thumb_h = bar_h * max_visible / count;
            if (thumb_h < 8) thumb_h = 8;
            int thumb_y = bar_y + (int)((bar_h - thumb_h) * t);
            SDL_Rect thumb = {bar_x, thumb_y, bar_w, thumb_h};
            SDL_Color thumb_col = kPalette.hover_fill;
            thumb_col.a = 200;
            SDL_SetRenderDrawColor(ctx->surface.renderer, thumb_col.r, thumb_col.g, thumb_col.b, thumb_col.a);
            SDL_RenderFillRect(ctx->surface.renderer, &thumb);
        }
        y = list_box.y + list_box.h + layout.line_height;
    }

    int btn_h = layout.line_height + 6;
    int margin = 24;
    int max_y = layout.panel.y + layout.panel.h - margin - btn_h;
    if (y > max_y) y = max_y;
    SDL_Rect cancel_btn[1];
    menu_vertical_buttons(&layout, cancel_btn, 1, y);
    bool cancel_focused = state->ui.menu_keyboard_active && state->ui.load_menu_focus == state->ui.load_count;
    draw_button(ctx, cancel_btn[0], "CANCEL", false, cancel_focused, layout.scale, mouse_x, mouse_y, allow_hover);
}

void ui_draw_load_confirm(RenderContext *ctx, const GameState *state) {
    const char *title = state->ui.load_is_replay ? "LOAD REPLAY?" : "LOAD SAVE?";
    MenuLayout layout = draw_panel_base(ctx, title, true);
    bool allow_hover = state->ui.menu_mouse_active;
    SDL_Rect buttons[2];
    int y = layout.body_y;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    if (state->ui.load_selected >= 0 && state->ui.load_selected < state->ui.load_count) {
        char label[64];
        strncpy(label, state->ui.load_names[state->ui.load_selected], sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        char *dot = strrchr(label, '.');
        if (dot) {
            if (state->ui.load_is_replay && strcmp(dot, ".replay") == 0) {
                *dot = '\0';
            } else if (!state->ui.load_is_replay && strcmp(dot, ".save") == 0) {
                *dot = '\0';
            }
        }
        draw_panel_text(ctx, &layout, label, y);
    }
    y += layout.line_height * 2;
    menu_vertical_buttons(&layout, buttons, 2, y);
    draw_button(ctx, buttons[0], "LOAD", false, state->ui.menu_keyboard_active && state->ui.load_confirm_index == 0,
        layout.scale, mouse_x, mouse_y, allow_hover);
    draw_button(ctx, buttons[1], "CANCEL", false, state->ui.menu_keyboard_active && state->ui.load_confirm_index == 1,
        layout.scale, mouse_x, mouse_y, allow_hover);
}

void ui_draw_main_menu(RenderContext *ctx, const GameState *state) {
    if (state->ui.show_new_menu) {
        ui_draw_new_menu(ctx, state);
        return;
    }
    SDL_SetRenderDrawColor(ctx->surface.renderer,
        kPalette.background.r, kPalette.background.g, kPalette.background.b, kPalette.background.a);
    SDL_RenderFillRect(ctx->surface.renderer, &(SDL_Rect){0, 0, ctx->surface.window_width, ctx->surface.window_height});

    draw_main_menu_art(ctx, ctx->surface.window_width, ctx->surface.window_height);

    MainMenuLayout layout = main_menu_layout(ctx->surface.window_width, ctx->surface.window_height);
    bool allow_hover = state->ui.menu_mouse_active;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    MenuButtons buttons = main_menu_buttons(&layout);
    draw_button(ctx, buttons.new_game, "NEW", false, state->ui.menu_keyboard_active && state->ui.main_menu_index == 0,
        layout.scale, mouse_x, mouse_y, allow_hover);
    draw_button(ctx, buttons.load, "LOAD", false, state->ui.menu_keyboard_active && state->ui.main_menu_index == 1,
        layout.scale, mouse_x, mouse_y, allow_hover);
    draw_button(ctx, buttons.replays, "REPLAYS", false, state->ui.menu_keyboard_active && state->ui.main_menu_index == 2,
        layout.scale, mouse_x, mouse_y, allow_hover);
    draw_button(ctx, buttons.quit, "QUIT", false, state->ui.menu_keyboard_active && state->ui.main_menu_index == 3,
        layout.scale, mouse_x, mouse_y, allow_hover);

    if (state->ui.show_load_menu) {
        ui_draw_load_menu(ctx, state);
    }
    if (state->ui.show_save_modal) {
        ui_draw_save_modal(ctx, state);
    }
    if (state->ui.show_load_confirm) {
        ui_draw_load_confirm(ctx, state);
    }
}

void ui_draw_pause_menu(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "PAUSE", false);
    bool allow_hover = state->ui.menu_mouse_active;
    SDL_Rect buttons[4];
    int y = layout.body_y;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    draw_panel_text(ctx, &layout, "GAME PAUSED", y);
    y += layout.line_height * 2;
    if (state->ui.replay_mode_active) {
        menu_vertical_buttons(&layout, buttons, 2, y);
        draw_button(ctx, buttons[0], "RESUME", false, state->ui.menu_keyboard_active && state->ui.pause_menu_index == 0,
            layout.scale, mouse_x, mouse_y, allow_hover);
        draw_button(ctx, buttons[1], "MAIN MENU", false, state->ui.menu_keyboard_active && state->ui.pause_menu_index == 1,
            layout.scale, mouse_x, mouse_y, allow_hover);
        y = buttons[1].y + buttons[1].h + layout.line_height;
        draw_panel_text(ctx, &layout, "ESC/P TO RESUME", y);
    } else {
        menu_vertical_buttons(&layout, buttons, 4, y);
        draw_button(ctx, buttons[0], "RESUME", false, state->ui.menu_keyboard_active && state->ui.pause_menu_index == 0,
            layout.scale, mouse_x, mouse_y, allow_hover);
        draw_button(ctx, buttons[1], "SAVE", false, state->ui.menu_keyboard_active && state->ui.pause_menu_index == 1,
            layout.scale, mouse_x, mouse_y, allow_hover);
        draw_button(ctx, buttons[2], "LOAD", false, state->ui.menu_keyboard_active && state->ui.pause_menu_index == 2,
            layout.scale, mouse_x, mouse_y, allow_hover);
        draw_button(ctx, buttons[3], "RESIGN", false, state->ui.menu_keyboard_active && state->ui.pause_menu_index == 3,
            layout.scale, mouse_x, mouse_y, allow_hover);
        y = buttons[3].y + buttons[3].h + layout.line_height;
        draw_panel_text(ctx, &layout, "ESC/P TO RESUME  Q/R TO RESIGN", y);
    }

    if (state->ui.show_load_menu) {
        ui_draw_load_menu(ctx, state);
    }
    if (state->ui.show_save_modal) {
        ui_draw_save_modal(ctx, state);
    }
    if (state->ui.show_load_confirm) {
        ui_draw_load_confirm(ctx, state);
    }
}

void ui_draw_win_screen(RenderContext *ctx, const GameState *state) {
    const char *title = "GAME OVER";
    char body[256] = {0};

    const char *phase = state->match.phase;
    const char *result = "DRAW";
    const char *cause = "UNKNOWN";
    if (strncmp(phase, "stalemate", 9) == 0) {
        title = "STALEMATE";
        result = "DRAW";
        cause = "STALEMATE";
    } else if (strncmp(phase, "draw_insufficient", 16) == 0) {
        title = "DRAW";
        result = "DRAW";
        cause = "INSUFFICIENT MATERIAL";
    } else if (strncmp(phase, "draw_threefold", 14) == 0) {
        title = "DRAW";
        result = "DRAW";
        cause = "THREEFOLD REPETITION";
    } else if (strncmp(phase, "draw_fifty", 10) == 0) {
        title = "DRAW";
        result = "DRAW";
        cause = "FIFTY-MOVE RULE";
    } else if (strncmp(phase, "draw", 4) == 0) {
        title = "DRAW";
        result = "DRAW";
        cause = "AGREED DRAW";
    } else if (strncmp(phase, "checkmate", 9) == 0 || strncmp(phase, "win_white", 9) == 0 || strncmp(phase, "win_black", 9) == 0) {
        bool white_wins = (strncmp(phase, "win_white", 9) == 0) || (!state->match.white_to_move && strncmp(phase, "checkmate", 9) == 0);
        bool black_wins = (strncmp(phase, "win_black", 9) == 0) || (state->match.white_to_move && strncmp(phase, "checkmate", 9) == 0);
        if (white_wins) {
            title = "WHITE WINS";
            result = "WHITE WINS";
        } else if (black_wins) {
            title = "BLACK WINS";
            result = "BLACK WINS";
        }
        if (strncmp(phase, "checkmate", 9) == 0) {
            cause = "CHECKMATE";
        } else if (strstr(phase, "_resign") != NULL) {
            cause = "RESIGNATION";
        } else if (strncmp(state->match.mode, "checkers", 8) == 0) {
            int white_pieces = 0;
            int black_pieces = 0;
            for (int r = 0; r < 8; ++r) {
                for (int f = 0; f < 8; ++f) {
                    char p = state->board_state.squares[r][f].piece;
                    if (p == '.') continue;
                    if (game_is_white_piece(p)) {
                        white_pieces++;
                    } else if (game_is_black_piece(p)) {
                        black_pieces++;
                    }
                }
            }
            if ((white_wins && black_pieces == 0) || (black_wins && white_pieces == 0)) {
                cause = "ALL PIECES CAPTURED";
            } else {
                cause = "NO LEGAL MOVES";
            }
        } else {
            cause = "WIN";
        }
    }

    MenuLayout layout = draw_panel_base(ctx, title, false);
    bool allow_hover = state->ui.menu_mouse_active;
    SDL_Rect buttons[2];
    int y = layout.body_y;
    char last_move[64];
    format_last_move(state, last_move, sizeof(last_move));
    snprintf(body, sizeof(body), "%s\nRESULT: %s\nCAUSE: %s\nLAST MOVE: %s",
        state->match.mode, result, cause, last_move);
    draw_panel_text(ctx, &layout, body, y);
    y += layout.line_height * 5;
    menu_vertical_buttons(&layout, buttons, 2, y);
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    bool save_disabled = state->ui.replay_saved || state->ui.replay_mode_active;
    bool save_focused = state->ui.menu_keyboard_active && state->ui.win_menu_index == 0;
    bool menu_focused = state->ui.menu_keyboard_active && state->ui.win_menu_index == 1;
    draw_button(ctx, buttons[0], "SAVE REPLAY", false, save_focused && !save_disabled,
        layout.scale, mouse_x, mouse_y, allow_hover && !save_disabled);
    if (save_disabled) {
        SDL_Color dim = kPalette.panel_fill;
        dim.a = 180;
        SDL_SetRenderDrawColor(ctx->surface.renderer, dim.r, dim.g, dim.b, dim.a);
        SDL_RenderFillRect(ctx->surface.renderer, &buttons[0]);
        SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g,
            kPalette.panel_border.b, kPalette.panel_border.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &buttons[0]);
        int text_w = measure_text_width("SAVE REPLAY", layout.scale);
        int text_x = buttons[0].x + (buttons[0].w - text_w) / 2;
        int text_y = buttons[0].y + (buttons[0].h - (7 + 1) * layout.scale) / 2;
        draw_text(ctx->surface.renderer, text_x, text_y, layout.scale, kPalette.label_text, "SAVE REPLAY");
    }
    draw_button(ctx, buttons[1], "MAIN MENU", false, menu_focused,
        layout.scale, mouse_x, mouse_y, allow_hover);
    if (state->ui.show_save_modal) {
        ui_draw_save_modal(ctx, state);
    }
}
