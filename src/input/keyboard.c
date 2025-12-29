#include "input/keyboard.h"

void keyboard_handle_event(GameState *game, const SDL_Event *event) {
    if (!game || !event) {
        return;
    }
    if (event->type != SDL_KEYDOWN) {
        return;
    }

    SDL_Keycode key = event->key.keysym.sym;
    switch (key) {
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (game->show_main_menu) {
                game->show_main_menu = false;
            }
            break;
        case SDLK_F3:
            game->show_debug = !game->show_debug;
            break;
        case SDLK_c:
            if (game->show_main_menu) {
                game_reset(game, "checkers");
            }
            break;
        case SDLK_h:
            if (game->show_main_menu) {
                game_reset(game, "chess");
            }
            break;
        case SDLK_p:
        case SDLK_ESCAPE:
            if (game->show_main_menu) {
                game->show_main_menu = false;
            } else {
                game->show_pause_menu = !game->show_pause_menu;
            }
            break;
        default:
            break;
    }
}
