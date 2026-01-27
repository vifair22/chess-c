#include "renderer/ui.h"

#include <SDL.h>
#include <string.h>

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
    int panel_w = window_w * 2 / 3;
    int panel_h = window_h * 2 / 3;
    layout.panel = center_rect(window_w, window_h, panel_w, panel_h);
    layout.scale = window_w / 300;
    if (layout.scale < 2) layout.scale = 2;
    layout.body_x = layout.panel.x + 24;
    layout.body_y = layout.panel.y + 24 + (8 * (layout.scale + 1));
    layout.line_height = (7 + 1) * layout.scale;
    return layout;
}

static MainMenuLayout main_menu_layout(int window_w, int window_h) {
    MainMenuLayout layout;
    layout.scale = window_w / 260;
    if (layout.scale < 3) layout.scale = 3;
    layout.line_height = (7 + 1) * layout.scale;
    layout.button_w = window_w * 2 / 3;
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
    SDL_Rect depth[4];
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

static void draw_panel_text(RenderContext *ctx, const MenuLayout *layout, const char *text, int y) {
    draw_text(ctx->surface.renderer, layout->body_x, y, layout->scale, kPalette.panel_text, text);
}

static void menu_vertical_buttons(const MenuLayout *layout, SDL_Rect *out, int count, int start_y) {
    int margin = 24;
    int gap = 12;
    int full_w = layout->panel.w - margin * 2;
    if (full_w < 0) full_w = layout->panel.w;
    int btn_h = layout->line_height + 6;
    int x = layout->panel.x + margin;
    int y = start_y;
    for (int i = 0; i < count; ++i) {
        out[i] = (SDL_Rect){x, y, full_w, btn_h};
        y += btn_h + gap;
    }
}

static void ui_draw_save_modal(RenderContext *ctx, const GameState *state);
static void ui_draw_load_menu(RenderContext *ctx, const GameState *state);
static void ui_draw_load_confirm(RenderContext *ctx, const GameState *state);
static void ui_draw_new_menu(RenderContext *ctx, const GameState *state);

static MenuButtons menu_buttons(const MenuLayout *layout) {
    MenuButtons buttons = {0};
    int margin = 24;
    int gap = 12;
    int full_w = layout->panel.w - margin * 2;
    if (full_w < 0) full_w = layout->panel.w;
    int btn_h = layout->line_height + 6;
    int y = layout->body_y;
    int x_left = layout->panel.x + margin;
    buttons.new_game = (SDL_Rect){x_left, y, full_w, btn_h};
    y += btn_h + gap;
    buttons.load = (SDL_Rect){x_left, y, full_w, btn_h};
    y += btn_h + gap;
    buttons.quit = (SDL_Rect){x_left, y, full_w, btn_h};
    return buttons;
}

static MenuButtons main_menu_buttons(const MainMenuLayout *layout) {
    MenuButtons buttons = {0};
    int gap = layout->line_height / 2;
    if (gap < 10) gap = 10;
    int y = layout->button_start_y;
    buttons.new_game = (SDL_Rect){layout->button_x, y, layout->button_w, layout->button_h};
    y += layout->button_h + gap;
    buttons.load = (SDL_Rect){layout->button_x, y, layout->button_w, layout->button_h};
    y += layout->button_h + gap;
    buttons.quit = (SDL_Rect){layout->button_x, y, layout->button_w, layout->button_h};
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
    int depth_w = (full_w - gap * 3) / 4;
    for (int i = 0; i < 4; ++i) {
        buttons.depth[i] = (SDL_Rect){x_left + i * (depth_w + gap), y, depth_w, btn_h};
    }
    y += btn_h + gap;

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

static void draw_button(RenderContext *ctx, SDL_Rect rect, const char *label, bool active, int scale) {
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    bool hovered = point_in_rect(mouse_x, mouse_y, rect);

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

    int text_w = measure_text_width(label, scale);
    int text_x = rect.x + (rect.w - text_w) / 2;
    int text_y = rect.y + (rect.h - (7 * scale)) / 2;
    draw_text(ctx->surface.renderer, text_x, text_y, scale, kPalette.panel_text, label);
}

static void draw_main_menu_art(RenderContext *ctx, int window_w, int window_h) {
    int scale = window_w / 180;
    if (scale < 4) scale = 4;
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

    int x = layout.body_x;
    int y = layout.body_y;
    draw_text(ctx->surface.renderer, x, y, layout.scale, kPalette.panel_text, "MODE");
    y += layout.line_height;

    bool chess_active = strncmp(state->match.mode, "chess", 5) == 0;
    draw_button(ctx, buttons.chess, "CHESS", chess_active, layout.scale);
    draw_button(ctx, buttons.checkers, "CHECKERS", !chess_active, layout.scale);

    y = buttons.ai_side.y - layout.line_height;
    draw_text(ctx->surface.renderer, x, y, layout.scale, kPalette.panel_text, "AI SIDE");
    draw_button(ctx, buttons.ai_side, ai_side_label(state->ai.side), false, layout.scale);

    y = buttons.depth[0].y - layout.line_height;
    draw_text(ctx->surface.renderer, x, y, layout.scale, kPalette.panel_text, "AI DEPTH");
    for (int i = 0; i < 4; ++i) {
        char label[2] = {(char)('1' + i), '\0'};
        draw_button(ctx, buttons.depth[i], label, state->ai.depth == (i + 1), layout.scale);
    }

    draw_button(ctx, buttons.start, "START", false, layout.scale);
    draw_button(ctx, buttons.back, "BACK", false, layout.scale);

    int hint_y = buttons.back.y + buttons.back.h + layout.line_height;
    draw_text(ctx->surface.renderer, x, hint_y, layout.scale, kPalette.panel_text, "ESC TO GO BACK");
}

static void ui_draw_save_modal(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "SAVE GAME", true);
    SDL_Rect buttons[2];
    int y = layout.body_y;
    draw_panel_text(ctx, &layout, "ENTER A NAME", y);
    y += layout.line_height + 6;

    SDL_Rect input = {layout.panel.x + 24, y, layout.panel.w - 48, layout.line_height + 10};
    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_fill.r, kPalette.panel_fill.g, kPalette.panel_fill.b, 255);
    SDL_RenderFillRect(ctx->surface.renderer, &input);
    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &input);
    draw_text(ctx->surface.renderer, input.x + 6, input.y + 5, layout.scale, kPalette.panel_text, state->ui.save_name);

    y = input.y + input.h + layout.line_height;
    menu_vertical_buttons(&layout, buttons, 2, y);
    draw_button(ctx, buttons[0], "SAVE", false, layout.scale);
    draw_button(ctx, buttons[1], "CANCEL", false, layout.scale);
}

static void ui_draw_load_menu(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "LOAD GAME", true);
    int y = layout.body_y;
    draw_panel_text(ctx, &layout, "SELECT A SAVE", y);
    y += layout.line_height + 6;

    int count = state->ui.load_count;
    if (count <= 0) {
        draw_panel_text(ctx, &layout, "NO SAVES FOUND", y);
        y += layout.line_height * 2;
    } else {
        int max_visible = 8;
        int start = state->ui.load_scroll;
        if (start < 0) start = 0;
        if (start > count - 1) start = count - 1;
        int visible = count - start;
        if (visible > max_visible) visible = max_visible;
        SDL_Rect list_box = {layout.panel.x + 24, y, layout.panel.w - 48,
                             visible * (layout.line_height + 6) + (visible - 1) * 12};
        SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_fill.r, kPalette.panel_fill.g, kPalette.panel_fill.b, 255);
        SDL_RenderFillRect(ctx->surface.renderer, &list_box);
        SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &list_box);

        SDL_Rect buttons[8];
        menu_vertical_buttons(&layout, buttons, visible, y);
        for (int i = 0; i < visible; ++i) {
            const char *name = state->ui.load_names[start + i];
            char label[64];
            strncpy(label, name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
            char *dot = strrchr(label, '.');
            if (dot && strcmp(dot, ".replay") == 0) {
                *dot = '\0';
            }
            draw_button(ctx, buttons[i], label, false, layout.scale);
        }
        if (count > max_visible) {
            if (start > 0) {
                draw_panel_text(ctx, &layout, "^ SCROLL", list_box.y - layout.line_height);
            }
            if (start + visible < count) {
                draw_panel_text(ctx, &layout, "v SCROLL", list_box.y + list_box.h + 4);
            }
        }
        y = list_box.y + list_box.h + layout.line_height;
    }

    SDL_Rect cancel_btn[1];
    menu_vertical_buttons(&layout, cancel_btn, 1, y);
    draw_button(ctx, cancel_btn[0], "CANCEL", false, layout.scale);
}

static void ui_draw_load_confirm(RenderContext *ctx, const GameState *state) {
    MenuLayout layout = draw_panel_base(ctx, "LOAD SAVE?", true);
    SDL_Rect buttons[2];
    int y = layout.body_y;
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
    draw_button(ctx, buttons[0], "LOAD", false, layout.scale);
    draw_button(ctx, buttons[1], "CANCEL", false, layout.scale);
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
    MenuButtons buttons = main_menu_buttons(&layout);
    draw_button(ctx, buttons.new_game, "NEW", false, layout.scale);
    draw_button(ctx, buttons.load, "LOAD", false, layout.scale);
    draw_button(ctx, buttons.quit, "QUIT", false, layout.scale);

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
    (void)state;
    MenuLayout layout = draw_panel_base(ctx, "PAUSE", false);
    SDL_Rect buttons[4];
    int y = layout.body_y;
    draw_panel_text(ctx, &layout, "GAME PAUSED", y);
    y += layout.line_height * 2;
    menu_vertical_buttons(&layout, buttons, 4, y);
    draw_button(ctx, buttons[0], "RESUME", false, layout.scale);
    draw_button(ctx, buttons[1], "SAVE", false, layout.scale);
    draw_button(ctx, buttons[2], "LOAD", false, layout.scale);
    draw_button(ctx, buttons[3], "RESIGN", false, layout.scale);
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
    if (strncmp(phase, "stalemate", 9) == 0) {
        title = "STALEMATE";
        result = "DRAW (STALEMATE)";
    } else if (strncmp(phase, "draw", 4) == 0) {
        title = "DRAW";
        result = "DRAW AGREED";
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
    }

    MenuLayout layout = draw_panel_base(ctx, title, false);
    SDL_Rect buttons[1];
    int y = layout.body_y;
    snprintf(body, sizeof(body), "%s\nRESULT: %s", state->match.mode, result);
    draw_panel_text(ctx, &layout, body, y);
    y += layout.line_height * 3;
    menu_vertical_buttons(&layout, buttons, 1, y);
    draw_button(ctx, buttons[0], "MAIN MENU", false, layout.scale);
}

bool ui_handle_main_menu_click(GameState *state, int mouse_x, int mouse_y, int window_w, int window_h, bool *out_request_quit) {
    if (!state) return false;
    if (out_request_quit) {
        *out_request_quit = false;
    }
    MainMenuLayout layout = main_menu_layout(window_w, window_h);

    if (state->ui.show_new_menu) {
        NewGameButtons buttons = new_game_buttons(&layout);
        if (point_in_rect(mouse_x, mouse_y, buttons.chess)) {
            game_reset(state, "chess");
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, buttons.checkers)) {
            game_reset(state, "checkers");
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
            return true;
        }
        for (int i = 0; i < 4; ++i) {
            if (point_in_rect(mouse_x, mouse_y, buttons.depth[i])) {
                state->ai.depth = i + 1;
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

    MenuButtons buttons = main_menu_buttons(&layout);
    if (point_in_rect(mouse_x, mouse_y, buttons.new_game)) {
        state->ui.show_new_menu = true;
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons.load)) {
        state->ui.show_load_menu = true;
        state->ui.load_needs_refresh = true;
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
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
        state->ui.show_save_modal = true;
        state->ui.save_len = 0;
        state->ui.save_name[0] = '\0';
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[2])) {
        state->ui.show_load_menu = true;
        state->ui.load_needs_refresh = true;
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[3])) {
        game_resign(state);
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
    int y = layout.body_y + layout.line_height * 3;
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
        return 1; // save
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
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
        int max_visible = 8;
        int start = state->ui.load_scroll;
        if (start < 0) start = 0;
        if (start > count - 1) start = count - 1;
        int visible = count - start;
        if (visible > max_visible) visible = max_visible;
        SDL_Rect buttons[8];
        menu_vertical_buttons(&layout, buttons, visible, y);
        for (int i = 0; i < visible; ++i) {
            if (point_in_rect(mouse_x, mouse_y, buttons[i])) {
                if (out_index) *out_index = start + i;
                return 1;
            }
        }
        y = buttons[visible - 1].y + buttons[visible - 1].h + layout.line_height;
    } else {
        y += layout.line_height * 2;
    }
    SDL_Rect cancel_btn[1];
    menu_vertical_buttons(&layout, cancel_btn, 1, y);
    if (point_in_rect(mouse_x, mouse_y, cancel_btn[0])) {
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
        return 1;
    }
    if (point_in_rect(mouse_x, mouse_y, buttons[1])) {
        return -1;
    }
    return 0;
}


