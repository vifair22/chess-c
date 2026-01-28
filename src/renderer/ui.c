#include "renderer/ui.h"

#include <SDL.h>
#include <string.h>
#include <time.h>

#include "renderer/draw.h"
#include "renderer/palette.h"
#include "save/replay.h"

static SDL_Rect center_rect(int window_w, int window_h, int w, int h) {
    SDL_Rect r;
    r.x = (window_w - w) / 2;
    r.y = (window_h - h) / 2;
    r.w = w;
    r.h = h;
    return r;
}


typedef struct {
    SDL_Rect panel;
    int scale;
    int body_x;
    int body_y;
    int line_height;
} MenuLayout;

typedef struct {
    int scale;
    int line_height;
    int button_w;
    int button_h;
    int button_x;
    int button_start_y;
} MainMenuLayout;

static MenuLayout menu_layout(int window_w, int window_h) {
    MenuLayout layout;
    int panel_w = window_w / 2;
    int panel_h = window_h * 2 / 3;
    layout.panel = center_rect(window_w, window_h, panel_w, panel_h);
    layout.scale = window_w / 300;
    if (layout.scale < 2) layout.scale = 2;
    layout.scale = (layout.scale + 1) / 2;
    if (layout.scale < 1) layout.scale = 1;
    layout.body_x = layout.panel.x + 24;
    layout.body_y = layout.panel.y + 24 + (8 * (layout.scale + 1));
    layout.line_height = (7 + 1) * layout.scale;
    return layout;
}

static MainMenuLayout main_menu_layout(int window_w, int window_h) {
    MainMenuLayout layout;
    layout.scale = window_w / 260;
    if (layout.scale < 3) layout.scale = 3;
    layout.scale = (layout.scale + 1) / 2;
    if (layout.scale < 1) layout.scale = 1;
    layout.line_height = (7 + 1) * layout.scale;
    layout.button_w = window_w / 2;
    if (layout.button_w < 220) layout.button_w = 220;
    layout.button_h = layout.line_height + 10;
    layout.button_x = (window_w - layout.button_w) / 2;
    layout.button_start_y = window_h * 5 / 10;
    return layout;
}

typedef struct {
    SDL_Rect new_game;
    SDL_Rect load;
    SDL_Rect quit;
} MenuButtons;

typedef struct {
    SDL_Rect chess;
    SDL_Rect checkers;
    SDL_Rect ai_side;
    SDL_Rect depth[9];
    SDL_Rect start;
    SDL_Rect back;
} NewGameButtons;

static int measure_text_width(const char *text, int scale) {
    const int glyph_w = 5;
    int line_width = 0;
    int max_width = 0;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n') {
            if (line_width > max_width) max_width = line_width;
            line_width = 0;
            continue;
        }
        line_width += (glyph_w + 1) * scale;
    }
    if (line_width > max_width) max_width = line_width;
    return max_width;
}

static bool point_in_rect(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

static void format_square(int file, int rank, char *out, size_t cap) {
    if (!out || cap < 2) return;
    if (file < 0 || file > 7 || rank < 0 || rank > 7) {
        snprintf(out, cap, "--");
        return;
    }
    char f = (char)('a' + file);
    int r = rank + 1;
    snprintf(out, cap, "%c%d", f, r);
}

static void format_last_move(const GameState *state, char *out, size_t cap) {
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

static void draw_panel_text(RenderContext *ctx, const MenuLayout *layout, const char *text, int y) {
    draw_text(ctx->surface.renderer, layout->body_x, y, layout->scale, kPalette.panel_text, text);
}

static void button_column(int x, int start_y, int width, int height, int gap, SDL_Rect *out, int count) {
    int y = start_y;
    for (int i = 0; i < count; ++i) {
        out[i] = (SDL_Rect){x, y, width, height};
        y += height + gap;
    }
}

static void menu_vertical_buttons(const MenuLayout *layout, SDL_Rect *out, int count, int start_y) {
    int margin = 24;
    int gap = 12;
    int full_w = layout->panel.w - margin * 2;
    if (full_w < 0) full_w = layout->panel.w;
    int btn_h = layout->line_height + 6;
    int x = layout->panel.x + margin;
    button_column(x, start_y, full_w, btn_h, gap, out, count);
}

static void ui_draw_save_modal(RenderContext *ctx, const GameState *state);
static void ui_draw_load_menu(RenderContext *ctx, const GameState *state);
static void ui_draw_load_confirm(RenderContext *ctx, const GameState *state);
static void ui_draw_new_menu(RenderContext *ctx, const GameState *state);

static MenuButtons menu_buttons(const MenuLayout *layout) {
    MenuButtons buttons = {0};
    SDL_Rect rects[3];
    menu_vertical_buttons(layout, rects, 3, layout->body_y);
    buttons.new_game = rects[0];
    buttons.load = rects[1];
    buttons.quit = rects[2];
    return buttons;
}

static MenuButtons main_menu_buttons(const MainMenuLayout *layout) {
    MenuButtons buttons = {0};
    int gap = layout->line_height / 2;
    if (gap < 10) gap = 10;
    SDL_Rect rects[3];
    button_column(layout->button_x, layout->button_start_y, layout->button_w, layout->button_h, gap, rects, 3);
    buttons.new_game = rects[0];
    buttons.load = rects[1];
    buttons.quit = rects[2];
    return buttons;
}

static NewGameButtons new_game_buttons(const MenuLayout *layout) {
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

static MenuLayout draw_panel_base(RenderContext *ctx, const char *title, bool opaque_backdrop) {
    MenuLayout layout = menu_layout(ctx->surface.window_width, ctx->surface.window_height);
    SDL_Rect panel = layout.panel;

    SDL_Color backdrop = kPalette.panel_backdrop;
    if (opaque_backdrop) {
        backdrop.a = 255;
    }
    SDL_SetRenderDrawColor(ctx->surface.renderer, backdrop.r, backdrop.g, backdrop.b, backdrop.a);
    SDL_RenderFillRect(ctx->surface.renderer, &(SDL_Rect){0, 0, ctx->surface.window_width, ctx->surface.window_height});

    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_fill.r, kPalette.panel_fill.g, kPalette.panel_fill.b, kPalette.panel_fill.a);
    SDL_RenderFillRect(ctx->surface.renderer, &panel);
    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &panel);

    int title_x = panel.x + 24;
    int title_y = panel.y + 24;
    draw_text(ctx->surface.renderer, title_x, title_y, layout.scale + 1, kPalette.panel_text, title);
    return layout;
}

static void draw_panel(RenderContext *ctx, const char *title, const char *body) {
    MenuLayout layout = draw_panel_base(ctx, title, false);
    draw_text(ctx->surface.renderer, layout.body_x, layout.body_y, layout.scale, kPalette.panel_text, body);
}

static void draw_button(RenderContext *ctx, SDL_Rect rect, const char *label, bool active, bool focused,
    int scale, int mouse_x, int mouse_y, bool allow_hover) {
    bool hovered = allow_hover && point_in_rect(mouse_x, mouse_y, rect);

    SDL_Color fill = kPalette.panel_fill;
    fill.r = (Uint8)(fill.r + 10);
    fill.g = (Uint8)(fill.g + 10);
    fill.b = (Uint8)(fill.b + 12);
    SDL_SetRenderDrawColor(ctx->surface.renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(ctx->surface.renderer, &rect);

    if (active) {
        SDL_Color sel = kPalette.select_fill;
        sel.a = 180;
        SDL_SetRenderDrawColor(ctx->surface.renderer, sel.r, sel.g, sel.b, sel.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    if (hovered) {
        SDL_Color hover = kPalette.hover_fill;
        hover.a = 140;
        SDL_SetRenderDrawColor(ctx->surface.renderer, hover.r, hover.g, hover.b, hover.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    if (focused && !hovered) {
        SDL_Color focus = kPalette.hover_fill;
        focus.a = 140;
        SDL_SetRenderDrawColor(ctx->surface.renderer, focus.r, focus.g, focus.b, focus.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &rect);
    if (hovered && rect.w > 6 && rect.h > 6) {
        SDL_Color hover_edge = kPalette.panel_border;
        hover_edge.r = (Uint8)((hover_edge.r + 40) > 255 ? 255 : (hover_edge.r + 40));
        hover_edge.g = (Uint8)((hover_edge.g + 40) > 255 ? 255 : (hover_edge.g + 40));
        hover_edge.b = (Uint8)((hover_edge.b + 40) > 255 ? 255 : (hover_edge.b + 40));
        SDL_Rect inner = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4};
        SDL_SetRenderDrawColor(ctx->surface.renderer, hover_edge.r, hover_edge.g, hover_edge.b, hover_edge.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &inner);
    }
    if (focused && !hovered && rect.w > 6 && rect.h > 6) {
        SDL_Color focus_edge = kPalette.panel_border;
        focus_edge.r = (Uint8)((focus_edge.r + 40) > 255 ? 255 : (focus_edge.r + 40));
        focus_edge.g = (Uint8)((focus_edge.g + 40) > 255 ? 255 : (focus_edge.g + 40));
        focus_edge.b = (Uint8)((focus_edge.b + 40) > 255 ? 255 : (focus_edge.b + 40));
        SDL_Rect inner = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4};
        SDL_SetRenderDrawColor(ctx->surface.renderer, focus_edge.r, focus_edge.g, focus_edge.b, focus_edge.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &inner);
    }

    int text_w = measure_text_width(label, scale);
    int text_x = rect.x + (rect.w - text_w) / 2;
    int text_y = rect.y + (rect.h - (7 * scale)) / 2;
    draw_text(ctx->surface.renderer, text_x, text_y, scale, kPalette.panel_text, label);
}

static void draw_button_lr(RenderContext *ctx, SDL_Rect rect, const char *left, const char *right,
    bool active, bool focused, int scale, int mouse_x, int mouse_y, bool allow_hover) {
    bool hovered = allow_hover && point_in_rect(mouse_x, mouse_y, rect);

    SDL_Color fill = kPalette.panel_fill;
    fill.r = (Uint8)(fill.r + 10);
    fill.g = (Uint8)(fill.g + 10);
    fill.b = (Uint8)(fill.b + 12);
    SDL_SetRenderDrawColor(ctx->surface.renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(ctx->surface.renderer, &rect);

    if (active) {
        SDL_Color sel = kPalette.select_fill;
        sel.a = 180;
        SDL_SetRenderDrawColor(ctx->surface.renderer, sel.r, sel.g, sel.b, sel.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    if (hovered) {
        SDL_Color hover = kPalette.hover_fill;
        hover.a = 140;
        SDL_SetRenderDrawColor(ctx->surface.renderer, hover.r, hover.g, hover.b, hover.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    if (focused && !hovered) {
        SDL_Color focus = kPalette.hover_fill;
        focus.a = 140;
        SDL_SetRenderDrawColor(ctx->surface.renderer, focus.r, focus.g, focus.b, focus.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &rect);
    if (hovered && rect.w > 6 && rect.h > 6) {
        SDL_Color hover_edge = kPalette.panel_border;
        hover_edge.r = (Uint8)((hover_edge.r + 40) > 255 ? 255 : (hover_edge.r + 40));
        hover_edge.g = (Uint8)((hover_edge.g + 40) > 255 ? 255 : (hover_edge.g + 40));
        hover_edge.b = (Uint8)((hover_edge.b + 40) > 255 ? 255 : (hover_edge.b + 40));
        SDL_Rect inner = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4};
        SDL_SetRenderDrawColor(ctx->surface.renderer, hover_edge.r, hover_edge.g, hover_edge.b, hover_edge.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &inner);
    }
    if (focused && !hovered && rect.w > 6 && rect.h > 6) {
        SDL_Color focus_edge = kPalette.panel_border;
        focus_edge.r = (Uint8)((focus_edge.r + 40) > 255 ? 255 : (focus_edge.r + 40));
        focus_edge.g = (Uint8)((focus_edge.g + 40) > 255 ? 255 : (focus_edge.g + 40));
        focus_edge.b = (Uint8)((focus_edge.b + 40) > 255 ? 255 : (focus_edge.b + 40));
        SDL_Rect inner = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4};
        SDL_SetRenderDrawColor(ctx->surface.renderer, focus_edge.r, focus_edge.g, focus_edge.b, focus_edge.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &inner);
    }

    int text_y = rect.y + (rect.h - (7 * scale)) / 2;
    int pad = 6;
    if (left && left[0]) {
        draw_text(ctx->surface.renderer, rect.x + pad, text_y, scale, kPalette.panel_text, left);
    }
    if (right && right[0]) {
        int right_w = measure_text_width(right, scale);
        int right_x = rect.x + rect.w - right_w - pad;
        if (right_x < rect.x + pad) right_x = rect.x + pad;
        draw_text(ctx->surface.renderer, right_x, text_y, scale, kPalette.panel_text, right);
    }
}

static void draw_main_menu_art(RenderContext *ctx, int window_w, int window_h) {
    int scale = window_w / 180;
    if (scale < 4) scale = 4;
    scale = (scale + 1) / 2;
    if (scale < 2) scale = 2;
    int text_w = measure_text_width("CHESS-C", scale);
    int x = (window_w - text_w) / 2;
    int y = window_h / 8;

    SDL_Color glow = kPalette.panel_border;
    glow.a = 200;
    for (int i = 3; i > 0; --i) {
        draw_text(ctx->surface.renderer, x - i, y - i, scale, glow, "CHESS-C");
        draw_text(ctx->surface.renderer, x + i, y + i, scale, glow, "CHESS-C");
    }
    draw_text(ctx->surface.renderer, x, y, scale, kPalette.panel_text, "CHESS-C");
}

static const char *ai_side_label(AiSide side) {
    switch (side) {
        case AI_SIDE_WHITE: return "WHITE";
        case AI_SIDE_BLACK: return "BLACK";
        default: return "OFF";
    }
}

static void ui_draw_new_menu(RenderContext *ctx, const GameState *state) {
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
        state->ui.menu_keyboard_active && state->ui.new_menu_index == 11, layout.scale, mouse_x, mouse_y, allow_hover);
    draw_button(ctx, buttons.back, "BACK", false,
        state->ui.menu_keyboard_active && state->ui.new_menu_index == 12, layout.scale, mouse_x, mouse_y, allow_hover);

    int hint_y = buttons.back.y + buttons.back.h + layout.line_height;
    draw_text(ctx->surface.renderer, x, hint_y, layout.scale, kPalette.panel_text, "ESC TO GO BACK");
}

static void ui_draw_save_modal(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "SAVE GAME", true);
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

static void ui_draw_load_menu(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "LOAD GAME", true);
    bool allow_hover = state->ui.menu_mouse_active;
    int y = layout.body_y;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    draw_panel_text(ctx, &layout, "SELECT A SAVE", y);
    y += layout.line_height + 6;

    int count = state->ui.load_count;
    if (count <= 0) {
        draw_panel_text(ctx, &layout, "NO SAVES FOUND", y);
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

        SDL_Rect buttons[10];
        int inner_pad = 8;
        int inner_x = list_box.x + inner_pad;
        int inner_w = list_box.w - inner_pad * 2;
        if (count > max_visible) {
            inner_w -= (bar_w + bar_pad);
        }
        int inner_y = list_box.y + inner_pad;
        int inner_h = list_row_h - 2;
        int inner_gap = list_gap;
        button_column(inner_x, inner_y, inner_w, inner_h, inner_gap, buttons, visible);
        for (int i = 0; i < visible; ++i) {
            const char *name = state->ui.load_names[start + i];
            char label[64];
            strncpy(label, name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
            char *dot = strrchr(label, '.');
            if (dot && strcmp(dot, ".replay") == 0) {
                *dot = '\0';
            }
            char time_buf[32] = {0};
            struct tm tm_local;
            if (localtime_s(&tm_local, &state->ui.load_times[start + i]) == 0) {
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", &tm_local);
            }
            int focus_index = start + i;
            bool focused = state->ui.menu_keyboard_active && state->ui.load_menu_focus == focus_index;
            draw_button_lr(ctx, buttons[i], label, time_buf, false, focused, layout.scale, mouse_x, mouse_y, allow_hover);
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

    SDL_Rect cancel_btn[1];
    menu_vertical_buttons(&layout, cancel_btn, 1, y);
    bool cancel_focused = state->ui.menu_keyboard_active && state->ui.load_menu_focus == state->ui.load_count;
    draw_button(ctx, cancel_btn[0], "CANCEL", false, cancel_focused, layout.scale, mouse_x, mouse_y, allow_hover);
}

static void ui_draw_load_confirm(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "LOAD SAVE?", true);
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
        if (dot && strcmp(dot, ".replay") == 0) {
            *dot = '\0';
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
    draw_button(ctx, buttons.quit, "QUIT", false, state->ui.menu_keyboard_active && state->ui.main_menu_index == 2,
        layout.scale, mouse_x, mouse_y, allow_hover);

    int hint_y = buttons.quit.y + buttons.quit.h + layout.line_height;
    int hint_w = measure_text_width("ENTER/SPACE FOR NEW GAME", layout.scale);
    int hint_x = (ctx->surface.window_width - hint_w) / 2;
    draw_text(ctx->surface.renderer, hint_x, hint_y, layout.scale, kPalette.panel_text, "ENTER/SPACE FOR NEW GAME");

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
        // In checkmate, the side to move is the loser.
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
    SDL_Rect buttons[1];
    int y = layout.body_y;
    char last_move[64];
    format_last_move(state, last_move, sizeof(last_move));
    snprintf(body, sizeof(body), "%s\nRESULT: %s\nCAUSE: %s\nLAST MOVE: %s",
        state->match.mode, result, cause, last_move);
    draw_panel_text(ctx, &layout, body, y);
    y += layout.line_height * 5;
    menu_vertical_buttons(&layout, buttons, 1, y);
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    draw_button(ctx, buttons[0], "MAIN MENU", false, state->ui.menu_keyboard_active,
        layout.scale, mouse_x, mouse_y, allow_hover);
}


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
        state->ui.pause_menu_index = 1;
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[2])) {
        state->ui.show_load_menu = true;
        state->ui.load_needs_refresh = true;
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
    return false;
}

bool ui_handle_win_screen_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h) {
    if (!state) return false;
    MenuLayout layout = menu_layout(window_w, window_h);
    if (mouse_x < layout.panel.x || mouse_x > layout.panel.x + layout.panel.w ||
        mouse_y < layout.panel.y || mouse_y > layout.panel.y + layout.panel.h) {
        return false;
    }
    SDL_Rect buttons[1];
    int y = layout.body_y + layout.line_height * 5;
    menu_vertical_buttons(&layout, buttons, 1, y);
    if (point_in_rect(mouse_x, mouse_y, buttons[0])) {
        game_reset(state, state->match.mode);
        state->ui.show_main_menu = true;
        state->ui.show_pause_menu = false;
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
        SDL_Rect buttons[10];
        menu_vertical_buttons(&layout, buttons, visible, y);
        for (int i = 0; i < visible; ++i) {
            if (point_in_rect(mouse_x, mouse_y, buttons[i])) {
                if (out_index) *out_index = start + i;
                state->ui.load_menu_focus = start + i;
                return 1;
            }
        }
        if (visible > 0) {
            y = buttons[visible - 1].y + buttons[visible - 1].h + layout.line_height;
        }
    } else {
        y += layout.line_height * 2;
    }
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
