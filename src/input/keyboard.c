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
        case SDLK_q:
            if (game->show_pause_menu) {
                game_resign(game);
            } else {
                game->promotion_choice_white = 'Q';
                game->promotion_choice_black = 'q';
            }
            break;
        case SDLK_r:
            if (game->show_pause_menu) {
                game_resign(game);
            } else {
                game->promotion_choice_white = 'R';
                game->promotion_choice_black = 'r';
            }
            break;
        case SDLK_b:
            if (!game->show_pause_menu) {
                game->promotion_choice_white = 'B';
                game->promotion_choice_black = 'b';
            }
            break;
        case SDLK_n:
            if (!game->show_pause_menu) {
                game->promotion_choice_white = 'N';
                game->promotion_choice_black = 'n';
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
