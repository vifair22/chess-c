#include "renderer/debug_overlay.h"

#include <stdio.h>

#include "renderer/draw.h"
#include "renderer/palette.h"

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

static int measure_text_lines(const char *text) {
    int lines = 1;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n') {
            ++lines;
        }
    }
    return lines;
}

void debug_overlay_render(RenderContext *ctx, const GameState *state) {
    if (!state) return;

    int scale = 2;
    int padding = 10;
    char body[256];

    // In this loop, render and tick are synced; show tick rate (fps equivalent).
    double fps = state->smoothed_fps > 0.0 ? state->smoothed_fps : state->last_fps;
    double ms = state->smoothed_frame_ms > 0.0 ? state->smoothed_frame_ms : state->last_frame_seconds * 1000.0;

    const char *ui_state = state->show_main_menu ? "main menu" :
                           state->show_pause_menu ? "pause menu" : "playing";

    snprintf(body, sizeof(body),
        "MODE: %s\n"
        "UI: %s\n"
        "GAME: %s\n"
        "TURN: %d\n"
        "TIME: %.0fs\n"
        "TPS: %.1f (%.2f ms)",
        state->game_mode,
        ui_state,
        state->game_phase,
        state->turn_number,
        state->game_time.total_seconds,
        fps,
        ms);

    int text_width = measure_text_width(body, scale);
    int lines = measure_text_lines(body);
    int line_height = (7 + 1) * scale;
    int text_height = lines * line_height;

    int box_w = text_width + padding * 2;
    int box_h = text_height + padding * 2;
    SDL_Rect box = {padding, ctx->window_height - box_h - padding, box_w, box_h};

    SDL_Color bg = kPalette.panel_backdrop;
    bg.a = 96; // more transparent
    SDL_SetRenderDrawColor(ctx->renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(ctx->renderer, &box);

    draw_text(ctx->renderer, box.x + padding, box.y + padding, scale, kPalette.panel_text, body);
}
