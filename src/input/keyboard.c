#include "input/keyboard.h"

static bool is_terminal_phase(const char *phase) {
    if (!phase) return false;
    return strncmp(phase, "checkmate", 9) == 0 ||
           strncmp(phase, "stalemate", 9) == 0 ||
           strncmp(phase, "win_", 4) == 0 ||
           strncmp(phase, "draw", 4) == 0;
}

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
            if (game->ui.show_main_menu) {
                game->ui.show_main_menu = false;
            } else if (is_terminal_phase(game->match.phase)) {
                game->ui.show_main_menu = true;
            }
            break;
        case SDLK_F3:
            game->ui.show_debug = !game->ui.show_debug;
            break;
        case SDLK_c:
            if (game->ui.show_main_menu) {
                game_reset(game, "checkers");
            }
            break;
        case SDLK_h:
            if (game->ui.show_main_menu) {
                game_reset(game, "chess");
            }
            break;
        case SDLK_q:
            if (game->ui.show_pause_menu) {
                game_resign(game);
            } else {
                game->variant.promotion_choice_white = 'Q';
                game->variant.promotion_choice_black = 'q';
            }
            break;
        case SDLK_r:
            if (game->ui.show_pause_menu) {
                game_resign(game);
            } else {
                game->variant.promotion_choice_white = 'R';
                game->variant.promotion_choice_black = 'r';
            }
            break;
        case SDLK_b:
            if (!game->ui.show_pause_menu) {
                game->variant.promotion_choice_white = 'B';
                game->variant.promotion_choice_black = 'b';
            }
            break;
        case SDLK_n:
            if (!game->ui.show_pause_menu) {
                game->variant.promotion_choice_white = 'N';
                game->variant.promotion_choice_black = 'n';
            }
            break;
        case SDLK_p:
        case SDLK_ESCAPE:
            if (game->ui.show_main_menu) {
                game->ui.show_main_menu = false;
            } else if (!is_terminal_phase(game->match.phase)) {
                game->ui.show_pause_menu = !game->ui.show_pause_menu;
            }
            break;
        default:
            break;
    }
}

