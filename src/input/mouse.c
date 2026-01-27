#include "input/mouse.h"

#include <SDL_log.h>
#include <string.h>

#include "save/replay.h"
#include "renderer/ui.h"

void mouse_handle_event(GameState *game, const RenderContext *render, const SDL_Event *event) {
    if (!game || !render || !event) {
        return;
    }

    if (event->type == SDL_MOUSEMOTION) {
        BoardGeometry geo = render_compute_board_geometry(render);
        int file = (event->motion.x - geo.offset_x) / geo.cell;
        int rank = (event->motion.y - geo.offset_y) / geo.cell;
        if (file < 0 || file >= 8 || rank < 0 || rank >= 8) {
            file = -1;
            rank = -1;
        }
        if (SDL_LockMutex(game->mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
            return;
        }
        game->moves.hover_pos = (BoardPos){file, rank};
        SDL_UnlockMutex(game->mutex);
        if (replay_global_recording()) {
            replay_recorder_record_hover(replay_global_recorder(), SDL_GetTicks(), file, rank);
        }
        return;
    }

    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT) {
        return;
    }

    if (game->ui.show_main_menu || game->ui.show_pause_menu ||
        strncmp(game->match.phase, "checkmate", 9) == 0 ||
        strncmp(game->match.phase, "stalemate", 9) == 0 ||
        strncmp(game->match.phase, "win_", 4) == 0 ||
        strncmp(game->match.phase, "draw", 4) == 0) {
        if (SDL_LockMutex(game->mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
            return;
        }
        bool handled = false;
        if (game->ui.show_main_menu) {
            handled = ui_handle_main_menu_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height);
        } else if (game->ui.show_pause_menu) {
            handled = ui_handle_pause_menu_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height);
        } else {
            handled = ui_handle_win_screen_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height);
        }
        SDL_UnlockMutex(game->mutex);
        if (handled) {
            return;
        }
        return; // ignore board clicks while menus or win screen are open
    }

    BoardGeometry geo = render_compute_board_geometry(render);
    int file = (event->button.x - geo.offset_x) / geo.cell;
    int rank = (event->button.y - geo.offset_y) / geo.cell;

    if (SDL_LockMutex(game->mutex) != 0) {
        SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        return;
    }

    game_handle_click(game, file, rank);
    if (replay_global_recording()) {
        replay_recorder_record_click(replay_global_recorder(), SDL_GetTicks(), file, rank);
    }
    SDL_UnlockMutex(game->mutex);
}

