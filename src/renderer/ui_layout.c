#include "renderer/ui_layout.h"
#include "game/logic.h"
#include <stdio.h>

SDL_Rect center_rect(int window_w, int window_h, int w, int h) {
    SDL_Rect r;
    r.x = (window_w - w) / 2;
    r.y = (window_h - h) / 2;
    r.w = w;
    r.h = h;
    return r;
}

int ui_scale_for_window(int window_w, int divisor, int min_scale) {
    int scale = window_w / divisor;
    if (scale < min_scale) scale = min_scale;
    scale = (scale + 1) / 2;
    if (scale < 1) scale = 1;
    return scale;
}

int ui_line_height(int scale) {
    return (7 + 1) * scale;
}

bool point_in_rect(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

void button_column(int x, int start_y, int width, int height, int gap, SDL_Rect *out, int count) {
    int y = start_y;
    for (int i = 0; i < count; ++i) {
        out[i] = (SDL_Rect){x, y, width, height};
        y += height + gap;
    }
}

void menu_vertical_buttons(const MenuLayout *layout, SDL_Rect *out, int count, int start_y) {
    int margin = 24;
    int gap = 12;
    int full_w = layout->panel.w - margin * 2;
    if (full_w < 0) full_w = layout->panel.w;
    int btn_h = layout->line_height + 6;
    int x = layout->panel.x + margin;
    button_column(x, start_y, full_w, btn_h, gap, out, count);
}

MenuLayout menu_layout(int window_w, int window_h) {
    MenuLayout layout;
    int panel_w = window_w / 2;
    int panel_h = window_h * 2 / 3;
    layout.panel = center_rect(window_w, window_h, panel_w, panel_h);
    layout.scale = ui_scale_for_window(window_w, 300, 2);
    layout.body_x = layout.panel.x + 24;
    layout.body_y = layout.panel.y + 24 + (8 * (layout.scale + 1));
    layout.line_height = ui_line_height(layout.scale);
    return layout;
}

MainMenuLayout main_menu_layout(int window_w, int window_h) {
    MainMenuLayout layout;
    layout.scale = ui_scale_for_window(window_w, 260, 3);
    layout.line_height = ui_line_height(layout.scale);
    layout.button_w = window_w / 2;
    if (layout.button_w < 220) layout.button_w = 220;
    layout.button_h = layout.line_height + 10;
    layout.button_x = (window_w - layout.button_w) / 2;
    layout.button_start_y = window_h * 5 / 10;
    return layout;
}

MenuButtons menu_buttons(const MenuLayout *layout) {
    MenuButtons buttons = {0};
    SDL_Rect rects[3];
    menu_vertical_buttons(layout, rects, 3, layout->body_y);
    buttons.new_game = rects[0];
    buttons.load = rects[1];
    buttons.quit = rects[2];
    return buttons;
}

MenuButtons main_menu_buttons(const MainMenuLayout *layout) {
    MenuButtons buttons = {0};
    int gap = layout->line_height / 2;
    if (gap < 10) gap = 10;
    SDL_Rect rects[4];
    button_column(layout->button_x, layout->button_start_y, layout->button_w, layout->button_h, gap, rects, 4);
    buttons.new_game = rects[0];
    buttons.load = rects[1];
    buttons.replays = rects[2];
    buttons.quit = rects[3];
    return buttons;
}

NewGameButtons new_game_buttons(const MenuLayout *layout) {
    NewGameButtons buttons = {0};
    int margin = 24;
    int gap = 12;
    int full_w = layout->panel.w - margin * 2;
    if (full_w < 0) full_w = layout->panel.w;
    int btn_h = layout->line_height + 6;
    int y = layout->body_y;
    int x_left = layout->panel.x + margin;

    y += layout->line_height; // MODE label
    int half_w = (full_w - gap) / 2;
    buttons.chess = (SDL_Rect){x_left, y, half_w, btn_h};
    buttons.checkers = (SDL_Rect){x_left + half_w + gap, y, half_w, btn_h};
    y += btn_h + gap;

    y += layout->line_height; // AI SIDE label
    buttons.ai_side = (SDL_Rect){x_left, y, full_w, btn_h};
    y += btn_h + gap;

    y += layout->line_height; // AI DEPTH label
    const int depth_cols = 3;
    const int depth_rows = 3;
    int depth_w = (full_w - gap * (depth_cols - 1)) / depth_cols;
    for (int row = 0; row < depth_rows; ++row) {
        for (int col = 0; col < depth_cols; ++col) {
            int i = row * depth_cols + col;
            buttons.depth[i] = (SDL_Rect){x_left + col * (depth_w + gap), y, depth_w, btn_h};
        }
        y += btn_h + gap;
    }

    buttons.start = (SDL_Rect){x_left, y, full_w, btn_h};
    y += btn_h + gap;
    buttons.back = (SDL_Rect){x_left, y, full_w, btn_h};
    return buttons;
}

void format_square(int file, int rank, char *out, size_t cap) {
    if (!out || cap < 2) return;
    if (file < 0 || file > 7 || rank < 0 || rank > 7) {
        snprintf(out, cap, "--");
        return;
    }
    char f = (char)('a' + file);
    int r = rank + 1;
    snprintf(out, cap, "%c%d", f, r);
}

void format_last_move(const GameState *state, char *out, size_t cap) {
    if (!state || !out || cap == 0) return;
    BoardPos from = state->last_move.start;
    BoardPos to = state->last_move.end;
    if (from.file < 0 || from.rank < 0 || to.file < 0 || to.rank < 0) {
        snprintf(out, cap, "N/A");
        return;
    }
    char from_buf[4];
    char to_buf[4];
    format_square(from.file, from.rank, from_buf, sizeof(from_buf));
    format_square(to.file, to.rank, to_buf, sizeof(to_buf));
    char piece = '.';
    if (to.file >= 0 && to.file < 8 && to.rank >= 0 && to.rank < 8) {
        piece = state->board_state.squares[to.rank][to.file].piece;
    }
    if (piece != '.') {
        snprintf(out, cap, "%c %s->%s", piece, from_buf, to_buf);
    } else {
        snprintf(out, cap, "%s->%s", from_buf, to_buf);
    }
}
