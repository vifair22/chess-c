#include "input/mouse.h"

#include <SDL_log.h>

void mouse_handle_event(GameState *game, const RenderContext *render, const SDL_Event *event) {
    if (!game || !render || !event) {
        return;
    }
    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT) {
        return;
    }

    if (game->show_main_menu || game->show_pause_menu) {
        return; // ignore board clicks while menus are open
    }

    BoardGeometry geo = render_compute_board_geometry(render);
    int file = (event->button.x - geo.offset_x) / geo.cell;
    int rank = (event->button.y - geo.offset_y) / geo.cell;

    if (SDL_LockMutex(game->mutex) != 0) {
        SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        return;
    }

    game_handle_click(game, file, rank);
    SDL_UnlockMutex(game->mutex);
}
