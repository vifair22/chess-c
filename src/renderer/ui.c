#include "renderer/ui.h"

#include <SDL.h>
#include <string.h>

#include "renderer/draw.h"
#include "renderer/palette.h"

static SDL_Rect center_rect(int window_w, int window_h, int w, int h) {
    SDL_Rect r;
    r.x = (window_w - w) / 2;
    r.y = (window_h - h) / 2;
    r.w = w;
    r.h = h;
    return r;
}

static void draw_panel(RenderContext *ctx, const char *title, const char *body) {
    const int panel_w = ctx->surface.window_width * 2 / 3;
    const int panel_h = ctx->surface.window_height * 2 / 3;
    SDL_Rect panel = center_rect(ctx->surface.window_width, ctx->surface.window_height, panel_w, panel_h);

    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_backdrop.r, kPalette.panel_backdrop.g, kPalette.panel_backdrop.b, kPalette.panel_backdrop.a);
    SDL_RenderFillRect(ctx->surface.renderer, &(SDL_Rect){0, 0, ctx->surface.window_width, ctx->surface.window_height});

    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_fill.r, kPalette.panel_fill.g, kPalette.panel_fill.b, kPalette.panel_fill.a);
    SDL_RenderFillRect(ctx->surface.renderer, &panel);
    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &panel);

    int scale = ctx->surface.window_width / 300;
    if (scale < 2) scale = 2;

    int title_x = panel.x + 24;
    int title_y = panel.y + 24;
    draw_text(ctx->surface.renderer, title_x, title_y, scale + 1, kPalette.panel_text, title);

    int body_x = panel.x + 24;
    int body_y = panel.y + 24 + (8 * (scale + 1));
    draw_text(ctx->surface.renderer, body_x, body_y, scale, kPalette.panel_text, body);
}

void ui_draw_main_menu(RenderContext *ctx, const GameState *state) {
    char body[256];
    snprintf(body, sizeof(body),
        "MODE: %s\n"
        "[H] CHESS   [C] CHECKERS\n"
        "LEFT CLICK TO SELECT\n"
        "ENTER / SPACE TO START\n"
        "ESC OR P TO TOGGLE PAUSE",
        state->match.mode);
    draw_panel(ctx, "MAIN MENU", body);
}

void ui_draw_pause_menu(RenderContext *ctx, const GameState *state) {
    (void)state;
    const char *body =
        "GAME PAUSED\n"
        "ESC OR P TO RESUME\n"
        "R OR Q TO RESIGN\n"
        "ENTER TO CLOSE MENU";
    draw_panel(ctx, "PAUSE", body);
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

    snprintf(body, sizeof(body),
        "%s\n"
        "RESULT: %s\n\n"
        "PRESS ENTER/SPACE TO RETURN TO MENU",
        state->match.mode, result);

    draw_panel(ctx, title, body);
}


