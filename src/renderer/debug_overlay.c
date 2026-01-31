#include "renderer/debug_overlay.h"

#include <stdio.h>

#include "ai/ai.h"
#include "renderer/draw.h"
#include "renderer/palette.h"

void debug_overlay_render(RenderContext *ctx, const GameState *state) {
    if (!state) return;

    int scale = 2;
    int padding = 10;
    char body[256];

    // In this loop, render and tick are synced; show tick rate (fps equivalent).
    double fps = state->perf.smoothed_fps > 0.0 ? state->perf.smoothed_fps : state->perf.last_fps;
    double ms = state->perf.smoothed_frame_ms > 0.0 ? state->perf.smoothed_frame_ms : state->perf.last_frame_seconds * 1000.0;

    const char *ui_state = state->ui.show_main_menu ? "main menu" :
                           state->ui.show_pause_menu ? "pause menu" : "playing";

    AiWorkerStatus ai_status = {0};
    bool ai_worker_ok = ai_get_worker_status(&ai_status);

    const char *ai_stage = "IDLE";
    if (ai_worker_ok && ai_status.ready) {
        switch (ai_status.stage) {
            case AI_STAGE_QUEUED: ai_stage = "QUEUED"; break;
            case AI_STAGE_GENERATE: ai_stage = "GENERATE"; break;
            case AI_STAGE_SEARCH: ai_stage = "SEARCH"; break;
            case AI_STAGE_DONE: ai_stage = "DONE"; break;
            default: ai_stage = "IDLE"; break;
        }
    } else {
        ai_stage = "DISABLED";
    }

    const char *ai_mode = (state->ai.depth >= 9) ? "TIME 10S" : "DEPTH";
    double ai_secs = (ai_worker_ok && ai_status.ready && ai_status.last_compute_ms > 0.0)
        ? ai_status.last_compute_ms / 1000.0 : 0.0;
    double ai_nps = (ai_secs > 0.0) ? (double)ai_status.last_nodes / ai_secs : 0.0;

    snprintf(body, sizeof(body),
        "MODE: %s\n"
        "UI: %s\n"
        "GAME: %s\n"
        "TURN: %d\n"
        "AI: %s D%d (%s)\n"
        "AI WORKER: %s (%s)%s%s\n"
        "AI JOB: T%d KEY %08X\n"
        "AI DEPTH: %d\n"
        "AI NODES: %llu\n"
        "AI NPS: %.0f\n"
        "AI LAST: %.2f ms\n"
        "TIME: %.0fs\n"
        "TPS: %.1f (%.2f ms)",
        state->match.mode,
        ui_state,
        state->match.phase,
        state->match.turn_number,
        state->ai.side == AI_SIDE_WHITE ? "WHITE" :
        state->ai.side == AI_SIDE_BLACK ? "BLACK" : "OFF",
        state->ai.depth,
        ai_mode,
        ai_worker_ok && ai_status.ready ? (ai_status.busy ? "BUSY" : "IDLE") : "DISABLED",
        ai_stage,
        ai_worker_ok && ai_status.ready && ai_status.has_job ? " (job)" : "",
        ai_worker_ok && ai_status.ready && ai_status.has_result ? " (result)" : "",
        ai_worker_ok ? ai_status.job_turn_number : -1,
        ai_worker_ok ? ai_status.job_position_key : 0,
        ai_status.last_depth,
        (unsigned long long)ai_status.last_nodes,
        ai_nps,
        ai_status.last_compute_ms,
        state->match.game_time.total_seconds,
        fps,
        ms);

    int text_width = measure_text_width(body, scale);
    int lines = measure_text_lines(body);
    int line_height = (7 + 1) * scale;
    int text_height = lines * line_height;

    int box_w = text_width + padding * 2;
    int box_h = text_height + padding * 2;
    SDL_Rect box = {padding, ctx->surface.window_height - box_h - padding, box_w, box_h};

    SDL_Color bg = kPalette.panel_backdrop;
    bg.a = 96; // more transparent
    SDL_SetRenderDrawColor(ctx->surface.renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(ctx->surface.renderer, &box);

    draw_text(ctx->surface.renderer, box.x + padding, box.y + padding, scale, kPalette.panel_text, body);
}
