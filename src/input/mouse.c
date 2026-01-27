#include "input/mouse.h"

#include <SDL_log.h>
#include <string.h>

#include "save/replay.h"
#include "save/save_handler.h"
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
        bool changed = false;
        if (SDL_LockMutex(game->mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
            return;
        }
        changed = (game->moves.hover_pos.file != file || game->moves.hover_pos.rank != rank);
        game->moves.hover_pos = (BoardPos){file, rank};
        SDL_UnlockMutex(game->mutex);
        (void)changed;
        return;
    }

    if (event->type == SDL_MOUSEWHEEL) {
        if (SDL_LockMutex(game->mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
            return;
        }
        if (game->ui.show_load_menu && game->ui.load_count > 0) {
            int delta = (event->wheel.y > 0) ? -1 : (event->wheel.y < 0 ? 1 : 0);
            int max_visible = 8;
            int max_start = game->ui.load_count - max_visible;
            if (max_start < 0) max_start = 0;
            game->ui.load_scroll += delta;
            if (game->ui.load_scroll < 0) game->ui.load_scroll = 0;
            if (game->ui.load_scroll > max_start) game->ui.load_scroll = max_start;
        }
        SDL_UnlockMutex(game->mutex);
        return;
    }

    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT) {
        return;
    }

    if (game->ui.show_save_modal || game->ui.show_load_menu ||
        game->ui.show_main_menu || game->ui.show_pause_menu ||
        strncmp(game->match.phase, "checkmate", 9) == 0 ||
        strncmp(game->match.phase, "stalemate", 9) == 0 ||
        strncmp(game->match.phase, "win_", 4) == 0 ||
        strncmp(game->match.phase, "draw", 4) == 0) {
        if (SDL_LockMutex(game->mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
            return;
        }
        bool handled = false;
        if (game->ui.show_load_confirm) {
            int action = ui_handle_load_confirm_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height);
            if (action == 1) {
                if (game->ui.load_selected >= 0 && game->ui.load_selected < game->ui.load_count) {
                    handled = save_load_named(game, game->ui.load_names[game->ui.load_selected]);
                }
                game->ui.show_load_confirm = false;
                game->ui.show_load_menu = false;
            } else if (action == -1) {
                game->ui.show_load_confirm = false;
                handled = true;
            }
        } else if (game->ui.show_save_modal) {
            int action = ui_handle_save_modal_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height);
            if (action == 1) {
                handled = save_write_named(game, game->ui.save_name);
                game->ui.show_save_modal = false;
            } else if (action == -1) {
                game->ui.show_save_modal = false;
                handled = true;
            }
        } else if (game->ui.show_load_menu) {
            if (game->ui.load_needs_refresh) {
                save_refresh_list(game);
                game->ui.load_needs_refresh = false;
            }
            int idx = -1;
            int action = ui_handle_load_menu_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height, &idx);
            if (action == 1 && idx >= 0 && idx < game->ui.load_count) {
                game->ui.load_selected = idx;
                game->ui.show_load_confirm = true;
                handled = true;
            } else if (action == -1) {
                game->ui.show_load_menu = false;
                handled = true;
            }
        } else if (game->ui.show_main_menu) {
            bool request_quit = false;
            handled = ui_handle_main_menu_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height, &request_quit);
            if (game->ui.load_needs_refresh) {
                save_refresh_list(game);
                game->ui.load_needs_refresh = false;
            }
            if (request_quit) {
                SDL_Event quit_event;
                quit_event.type = SDL_QUIT;
                SDL_PushEvent(&quit_event);
            }
        } else if (game->ui.show_pause_menu) {
            handled = ui_handle_pause_menu_click(game, event->button.x, event->button.y,
                render->surface.window_width, render->surface.window_height);
            if (game->ui.load_needs_refresh) {
                save_refresh_list(game);
                game->ui.load_needs_refresh = false;
            }
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

    LastMove before = game->last_move;
    BoardState before_board = game->board_state;
    game_handle_click(game, file, rank);
    LastMove after = game->last_move;
    bool moved = (before.start.file != after.start.file) || (before.start.rank != after.start.rank) ||
                 (before.end.file != after.end.file) || (before.end.rank != after.end.rank);
    if (replay_global_recording()) {
        if (moved) {
            char promo = '\0';
            char moved_piece = game->board_state.squares[after.end.rank][after.end.file].piece;
            char from_piece = '.';
            if (after.start.file >= 0 && after.start.file < 8 && after.start.rank >= 0 && after.start.rank < 8) {
                from_piece = before_board.squares[after.start.rank][after.start.file].piece;
            }
            if (moved_piece != '.' &&
                ((from_piece == 'P' || from_piece == 'p') ||
                 (from_piece == 'M' || from_piece == 'm'))) {
                promo = moved_piece;
            }
            replay_recorder_record_move(replay_global_recorder(), SDL_GetTicks(),
                after.start.file, after.start.rank, after.end.file, after.end.rank, promo);
        } else if (file >= 0 && file < 8 && rank >= 0 && rank < 8) {
            replay_recorder_record_click(replay_global_recorder(), SDL_GetTicks(), file, rank);
        }
    }
    SDL_UnlockMutex(game->mutex);
}

