#include "input/keyboard.h"

#include <ctype.h>

#include "save/save_handler.h"
#include "save/replay.h"

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
    if (game->ui.show_save_modal) {
        if (event->type == SDL_TEXTINPUT) {
            const char *text = event->text.text;
            for (const char *p = text; *p && game->ui.save_len < (int)sizeof(game->ui.save_name) - 1; ++p) {
                unsigned char c = (unsigned char)*p;
                if (isalnum(c) || c == '-' || c == '_' || c == ' ') {
                    game->ui.save_name[game->ui.save_len++] = (char)c;
                }
            }
            game->ui.save_name[game->ui.save_len] = '\0';
            return;
        }
        if (event->type != SDL_KEYDOWN) {
            return;
        }
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_BACKSPACE && game->ui.save_len > 0) {
            game->ui.save_name[--game->ui.save_len] = '\0';
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (save_write_named(game, game->ui.save_name)) {
                game->ui.show_save_modal = false;
            }
            return;
        }
        if (key == SDLK_ESCAPE) {
            game->ui.show_save_modal = false;
            return;
        }
        return;
    }

    if (game->ui.show_load_menu) {
        if (event->type != SDL_KEYDOWN) {
            return;
        }
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_ESCAPE) {
            game->ui.show_load_menu = false;
            return;
        }
        return;
    }

    if (game->ui.show_load_confirm) {
        if (event->type != SDL_KEYDOWN) {
            return;
        }
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_ESCAPE) {
            game->ui.show_load_confirm = false;
            return;
        }
        return;
    }

    if (event->type != SDL_KEYDOWN) {
        return;
    }

    SDL_Keycode key = event->key.keysym.sym;
    if (game->ui.show_main_menu) {
        switch (key) {
            case SDLK_RETURN:
            case SDLK_SPACE:
                if (game->ui.show_new_menu) {
                    game->ui.show_main_menu = false;
                    game->ui.show_new_menu = false;
                } else {
                    game->ui.show_new_menu = true;
                }
                break;
            case SDLK_h:
                if (game->ui.show_new_menu) {
                    game_reset(game, "chess");
                }
                break;
            case SDLK_c:
                if (game->ui.show_new_menu) {
                    game_reset(game, "checkers");
                }
                break;
            case SDLK_a:
                if (game->ui.show_new_menu) {
                    if (game->ai.side == AI_SIDE_NONE) {
                        game->ai.side = AI_SIDE_WHITE;
                    } else if (game->ai.side == AI_SIDE_WHITE) {
                        game->ai.side = AI_SIDE_BLACK;
                    } else {
                        game->ai.side = AI_SIDE_NONE;
                    }
                }
                break;
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
            case SDLK_4:
                if (game->ui.show_new_menu) {
                    game->ai.depth = (int)(key - SDLK_0);
                }
                break;
            case SDLK_ESCAPE:
                if (game->ui.show_new_menu) {
                    game->ui.show_new_menu = false;
                }
                break;
            default:
                break;
        }
        return;
    }

    switch (key) {
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (is_terminal_phase(game->match.phase)) {
                game_reset(game, game->match.mode);
                game->ui.show_main_menu = true;
                game->ui.show_pause_menu = false;
                replay_global_stop();
            }
            break;
        case SDLK_F3:
            game->ui.show_debug = !game->ui.show_debug;
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
            if (!is_terminal_phase(game->match.phase)) {
                game->ui.show_pause_menu = !game->ui.show_pause_menu;
            }
            break;
        case SDLK_u:
            if (!game->ui.show_main_menu &&
                !game->ui.show_pause_menu &&
                !is_terminal_phase(game->match.phase) &&
                !game->ui.undo_used) {
                if (game->ai.side != AI_SIDE_NONE) {
                    bool player_is_white = game->ai.side == AI_SIDE_BLACK;
                    bool next_turn_white = player_is_white;
                    while (game_peek_undo_white_to_move(game, &next_turn_white)) {
                        bool update_anim = next_turn_white == player_is_white;
                        if (!game_undo_ex(game, update_anim)) {
                            break;
                        }
                        if (update_anim) {
                            break;
                        }
                    }
                } else {
                    game_undo(game);
                }
                game->ui.undo_used = true;
            }
            break;
        default:
            break;
    }
}

