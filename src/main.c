#include <SDL.h>
#include <stdbool.h>

#include "game/logic.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "renderer/render.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    RenderContext render = {0};
    GameState game = {0};

    if (!render_init(&render, "Chess (SDL2)", 800, 800)) {
        return 1;
    }

    if (!game_init(&game)) {
        render_shutdown(&render);
        return 1;
    }

    Uint32 last_ticks = SDL_GetTicks();
    bool running = true;
    while (running) {
        Uint32 now = SDL_GetTicks();
        double delta_seconds = (now - last_ticks) / 1000.0;
        last_ticks = now;
        double fps = delta_seconds > 0.0 ? 1.0 / delta_seconds : 0.0;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
            keyboard_handle_event(&game, &e);
            mouse_handle_event(&game, &render, &e);
        }

        if (SDL_LockMutex(game.mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        } else {
            if (!game.show_main_menu && !game.show_pause_menu) {
                game.game_time.total_seconds += delta_seconds;
            }
            game.last_frame_seconds = delta_seconds;
            game.last_fps = fps;
            game.perf_accum_time += delta_seconds;
            game.perf_accum_frames += 1;
            if (game.perf_accum_time >= 0.5) { // update display every ~0.5s
                double avg_ms = (game.perf_accum_time / game.perf_accum_frames) * 1000.0;
                double avg_fps = game.perf_accum_frames / game.perf_accum_time;
                game.smoothed_frame_ms = avg_ms;
                game.smoothed_fps = avg_fps;
                game.perf_accum_time = 0.0;
                game.perf_accum_frames = 0;
            }
            SDL_UnlockMutex(game.mutex);
        }

        render_frame(&render, &game);
    }

    game_destroy(&game);
    render_shutdown(&render);
    return 0;
}
