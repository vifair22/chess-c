#include "input/mouse.h"

#include <SDL_log.h>
#include <stdlib.h>
#include <string.h>

#include "save/replay.h"
#include "save/save_handler.h"
#include "renderer/ui.h"

typedef struct {
    SDL_Rect panel;
    int scale;
    int body_y;
    int line_height;
} MenuLayout;

static SDL_Rect center_rect(int window_w, int window_h, int w, int h) {
    SDL_Rect r;
    r.x = (window_w - w) / 2;
    r.y = (window_h - h) / 2;
    r.w = w;
    r.h = h;
    return r;
}

static MenuLayout menu_layout(int window_w, int window_h) {
    MenuLayout layout;
    int panel_w = window_w * 2 / 3;
    int panel_h = window_h * 2 / 3;
    layout.panel = center_rect(window_w, window_h, panel_w, panel_h);
    layout.scale = window_w / 300;
    if (layout.scale < 2) layout.scale = 2;
    layout.body_y = layout.panel.y + 24 + (8 * (layout.scale + 1));
    layout.line_height = (7 + 1) * layout.scale;
    return layout;
}

static bool point_in_rect(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

static void load_menu_scrollbar_metrics(int window_w, int window_h, SDL_Rect *out_list_box,
    SDL_Rect *out_track, int *out_max_visible) {
    MenuLayout layout = menu_layout(window_w, window_h);
    int max_visible = 10;
    int y = layout.body_y + layout.line_height + 6;
    int list_row_h = layout.line_height + 6;
    int list_gap = 12;
    int list_total_h = max_visible * list_row_h + (max_visible - 1) * list_gap;
    SDL_Rect list_box = {layout.panel.x + 24, y, layout.panel.w - 48, list_total_h};
    int bar_w = 6;
    int bar_x = list_box.x + list_box.w - bar_w - 4;
    int bar_y = list_box.y + 4;
    int bar_h = list_box.h - 8;
    if (bar_h < 8) bar_h = 8;
    SDL_Rect track = {bar_x, bar_y, bar_w, bar_h};
    if (out_list_box) *out_list_box = list_box;
    if (out_track) *out_track = track;
    if (out_max_visible) *out_max_visible = max_visible;
}

static void update_load_scroll_from_thumb(GameState *game, int mouse_y, SDL_Rect track, int max_visible) {
    int count = game->ui.load_count;
    int max_start = count - max_visible;
    if (max_start < 1) max_start = 1;
    int bar_h = track.h;
    int thumb_h = bar_h * max_visible / count;
    if (thumb_h < 8) thumb_h = 8;
    int usable = bar_h - thumb_h;
    int top = track.y;
    int thumb_y = mouse_y - game->ui.load_scroll_drag_offset;
    if (thumb_y < top) thumb_y = top;
    if (thumb_y > top + usable) thumb_y = top + usable;
    float t = usable > 0 ? (float)(thumb_y - top) / (float)usable : 0.0f;
    int new_start = (int)(t * max_start + 0.5f);
    if (new_start < 0) new_start = 0;
    if (new_start > max_start) new_start = max_start;
    game->ui.load_scroll = new_start;
}

void mouse_handle_event(GameState *game, const RenderContext *render, const SDL_Event *event) {
    static int last_menu_x = 0;
    static int last_menu_y = 0;
    static bool last_menu_valid = false;
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
        if (game->ui.show_main_menu || game->ui.show_pause_menu ||
            game->ui.show_save_modal || game->ui.show_load_menu ||
            game->ui.show_load_confirm || game->ui.show_new_menu) {
            int dx = last_menu_valid ? abs(event->motion.x - last_menu_x) : 0;
            int dy = last_menu_valid ? abs(event->motion.y - last_menu_y) : 0;
            last_menu_x = event->motion.x;
            last_menu_y = event->motion.y;
            last_menu_valid = true;
            if (dx + dy >= 4) { // debounce: require a small movement
                game->ui.menu_keyboard_active = false;
                game->ui.menu_mouse_active = true;
            }
        } else {
            last_menu_valid = false;
        }
        if (game->ui.show_load_menu && game->ui.load_scroll_dragging) {
            SDL_Rect list_box = {0};
            SDL_Rect track = {0};
            int max_visible = 10;
            load_menu_scrollbar_metrics(render->surface.window_width, render->surface.window_height,
                &list_box, &track, &max_visible);
            update_load_scroll_from_thumb(game, event->motion.y, track, max_visible);
            game->ui.menu_keyboard_active = false;
            game->ui.menu_mouse_active = true;
        }
        SDL_UnlockMutex(game->mutex);
        (void)changed;
        return;
    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        if (SDL_LockMutex(game->mutex) == 0) {
            game->ui.load_scroll_dragging = false;
            SDL_UnlockMutex(game->mutex);
        }
        return;
    }

    if (event->type == SDL_MOUSEWHEEL) {
        if (SDL_LockMutex(game->mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
            return;
        }
        if (game->ui.show_load_menu && game->ui.load_count > 0) {
            int delta = (event->wheel.y > 0) ? -1 : (event->wheel.y < 0 ? 1 : 0);
            int max_visible = 10;
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
        game_is_terminal_phase(game->match.phase)) {
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
                if (save_write_named(game, game->ui.save_name)) {
                    handled = true;
                    game->ui.show_save_modal = false;
                }
            } else if (action == -1) {
                game->ui.show_save_modal = false;
                handled = true;
            }
        } else if (game->ui.show_load_menu) {
            if (game->ui.load_needs_refresh) {
                save_refresh_list(game);
                game->ui.load_needs_refresh = false;
            }
            SDL_Rect list_box = {0};
            SDL_Rect track = {0};
            int max_visible = 10;
            load_menu_scrollbar_metrics(render->surface.window_width, render->surface.window_height,
                &list_box, &track, &max_visible);
            if (point_in_rect(event->button.x, event->button.y, track)) {
                int count = game->ui.load_count;
                int max_start = count - max_visible;
                if (max_start < 1) max_start = 1;
                int thumb_h = track.h * max_visible / count;
                if (thumb_h < 8) thumb_h = 8;
                int thumb_y = track.y + (int)((track.h - thumb_h) * ((float)game->ui.load_scroll / (float)max_start));
                if (event->button.y >= thumb_y && event->button.y <= thumb_y + thumb_h) {
                    game->ui.load_scroll_drag_offset = event->button.y - thumb_y;
                } else {
                    game->ui.load_scroll_drag_offset = thumb_h / 2;
                    update_load_scroll_from_thumb(game, event->button.y, track, max_visible);
                }
                game->ui.load_scroll_dragging = true;
                game->ui.menu_keyboard_active = false;
                game->ui.menu_mouse_active = true;
                handled = true;
            } else {
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

