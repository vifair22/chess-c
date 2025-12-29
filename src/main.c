#include <SDL.h>
#include <stdbool.h>

#include "game/logic.h"
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

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                BoardGeometry geo = render_compute_board_geometry(&render);
                int file = (e.button.x - geo.offset_x) / geo.cell;
                int rank = (e.button.y - geo.offset_y) / geo.cell;
                if (SDL_LockMutex(game.mutex) != 0) {
                    SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
                } else {
                    game_handle_click(&game, file, rank);
                    SDL_UnlockMutex(game.mutex);
                }
            }
        }

        if (SDL_LockMutex(game.mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        } else {
            game.game_time.total_seconds += delta_seconds;
            SDL_UnlockMutex(game.mutex);
        }

        render_frame(&render, &game);
    }

    game_destroy(&game);
    render_shutdown(&render);
    return 0;
}
