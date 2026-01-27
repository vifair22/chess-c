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

static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void ensure_load_focus_visible(GameState *game, int max_visible) {
    int count = game->ui.load_count;
    int focus = game->ui.load_menu_focus;
    if (count <= 0) {
        game->ui.load_scroll = 0;
        game->ui.load_menu_focus = count;
        return;
    }
    if (focus < 0) focus = 0;
    if (focus >= count) focus = count - 1;
    game->ui.load_menu_focus = focus;
    if (game->ui.load_scroll > focus) {
        game->ui.load_scroll = focus;
    } else if (game->ui.load_scroll + max_visible - 1 < focus) {
        game->ui.load_scroll = focus - max_visible + 1;
    }
    if (game->ui.load_scroll < 0) game->ui.load_scroll = 0;
    int max_start = count - max_visible;
    if (max_start < 0) max_start = 0;
    if (game->ui.load_scroll > max_start) game->ui.load_scroll = max_start;
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
        if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT) {
            if (!game->ui.menu_keyboard_active) {
                game->ui.menu_keyboard_active = true;
                game->ui.menu_mouse_active = false;
                return;
            }
            game->ui.save_menu_index = (game->ui.save_menu_index == 0) ? 1 : 0;
            return;
        }
        if (key == SDLK_BACKSPACE && game->ui.save_len > 0) {
            game->ui.save_name[--game->ui.save_len] = '\0';
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (game->ui.save_menu_index == 1) {
                game->ui.show_save_modal = false;
                return;
            }
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
        int count = game->ui.load_count;
        int max_visible = 8;
        if (key == SDLK_ESCAPE) {
            game->ui.show_load_menu = false;
            return;
        }
        if (key == SDLK_DOWN) {
            if (!game->ui.menu_keyboard_active) {
                game->ui.menu_keyboard_active = true;
                game->ui.menu_mouse_active = false;
                return;
            }
            int max_focus = count;
            game->ui.load_menu_focus = clamp_int(game->ui.load_menu_focus + 1, 0, max_focus);
            ensure_load_focus_visible(game, max_visible);
            return;
        }
        if (key == SDLK_UP) {
            if (!game->ui.menu_keyboard_active) {
                game->ui.menu_keyboard_active = true;
                game->ui.menu_mouse_active = false;
                return;
            }
            int max_focus = count;
            game->ui.load_menu_focus = clamp_int(game->ui.load_menu_focus - 1, 0, max_focus);
            ensure_load_focus_visible(game, max_visible);
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (count > 0 && game->ui.load_menu_focus < count) {
                game->ui.load_selected = game->ui.load_menu_focus;
                game->ui.show_load_confirm = true;
                game->ui.load_confirm_index = 0;
            } else {
                game->ui.show_load_menu = false;
            }
            return;
        }
        return;
    }

    if (game->ui.show_load_confirm) {
        if (event->type != SDL_KEYDOWN) {
            return;
        }
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) {
            if (!game->ui.menu_keyboard_active) {
                game->ui.menu_keyboard_active = true;
                game->ui.menu_mouse_active = false;
                return;
            }
            game->ui.load_confirm_index = (game->ui.load_confirm_index == 0) ? 1 : 0;
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (game->ui.load_confirm_index == 0) {
                if (game->ui.load_selected >= 0 && game->ui.load_selected < game->ui.load_count) {
                    save_load_named(game, game->ui.load_names[game->ui.load_selected]);
                }
            }
            game->ui.show_load_confirm = false;
            game->ui.show_load_menu = false;
            return;
        }
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
        if (game->ui.show_new_menu) {
            int idx = game->ui.new_menu_index;
            if (key == SDLK_LEFT) {
                if (!game->ui.menu_keyboard_active) {
                    game->ui.menu_keyboard_active = true;
                    game->ui.menu_mouse_active = false;
                    return;
                }
                if (idx == 1) idx = 0;
                else if (idx == 0) idx = 1;
                else if (idx >= 3 && idx <= 6) idx = clamp_int(idx - 1, 3, 6);
                game->ui.new_menu_index = idx;
                return;
            }
            if (key == SDLK_RIGHT) {
                if (!game->ui.menu_keyboard_active) {
                    game->ui.menu_keyboard_active = true;
                    game->ui.menu_mouse_active = false;
                    return;
                }
                if (idx == 0) idx = 1;
                else if (idx == 1) idx = 0;
                else if (idx >= 3 && idx <= 6) idx = clamp_int(idx + 1, 3, 6);
                game->ui.new_menu_index = idx;
                return;
            }
            if (key == SDLK_UP) {
                if (!game->ui.menu_keyboard_active) {
                    game->ui.menu_keyboard_active = true;
                    game->ui.menu_mouse_active = false;
                    return;
                }
                game->ui.new_menu_index = clamp_int(idx - 1, 0, 8);
                return;
            }
            if (key == SDLK_DOWN) {
                if (!game->ui.menu_keyboard_active) {
                    game->ui.menu_keyboard_active = true;
                    game->ui.menu_mouse_active = false;
                    return;
                }
                game->ui.new_menu_index = clamp_int(idx + 1, 0, 8);
                return;
            }
            if (key == SDLK_ESCAPE) {
                game->ui.show_new_menu = false;
                return;
            }
            if (key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_KP_ENTER) {
                switch (idx) {
                    case 0:
                        game_reset(game, "chess");
                        break;
                    case 1:
                        game_reset(game, "checkers");
                        break;
                    case 2:
                        if (game->ai.side == AI_SIDE_NONE) {
                            game->ai.side = AI_SIDE_WHITE;
                        } else if (game->ai.side == AI_SIDE_WHITE) {
                            game->ai.side = AI_SIDE_BLACK;
                        } else {
                            game->ai.side = AI_SIDE_NONE;
                        }
                        break;
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                        game->ai.depth = (idx - 2);
                        break;
                    case 7:
                        replay_global_stop();
                        replay_global_start(game, SDL_GetTicks());
                        game->ui.show_main_menu = false;
                        game->ui.show_new_menu = false;
                        break;
                    case 8:
                        game->ui.show_new_menu = false;
                        break;
                    default:
                        break;
                }
                return;
            }
            switch (key) {
                case SDLK_h:
                    game_reset(game, "chess");
                    return;
                case SDLK_c:
                    game_reset(game, "checkers");
                    return;
                case SDLK_a:
                    if (game->ai.side == AI_SIDE_NONE) {
                        game->ai.side = AI_SIDE_WHITE;
                    } else if (game->ai.side == AI_SIDE_WHITE) {
                        game->ai.side = AI_SIDE_BLACK;
                    } else {
                        game->ai.side = AI_SIDE_NONE;
                    }
                    return;
                case SDLK_1:
                case SDLK_2:
                case SDLK_3:
                case SDLK_4:
                    game->ai.depth = (int)(key - SDLK_0);
                    return;
                default:
                    return;
            }
        } else {
            if (key == SDLK_UP) {
                if (!game->ui.menu_keyboard_active) {
                    game->ui.menu_keyboard_active = true;
                    game->ui.menu_mouse_active = false;
                    return;
                }
                game->ui.main_menu_index = clamp_int(game->ui.main_menu_index - 1, 0, 2);
                return;
            }
            if (key == SDLK_DOWN) {
                if (!game->ui.menu_keyboard_active) {
                    game->ui.menu_keyboard_active = true;
                    game->ui.menu_mouse_active = false;
                    return;
                }
                game->ui.main_menu_index = clamp_int(game->ui.main_menu_index + 1, 0, 2);
                return;
            }
            if (key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_KP_ENTER) {
                if (game->ui.main_menu_index == 0) {
                    game->ui.show_new_menu = true;
                    game->ui.new_menu_index = 0;
                } else if (game->ui.main_menu_index == 1) {
                    game->ui.show_load_menu = true;
                    game->ui.load_needs_refresh = true;
                    game->ui.load_menu_focus = 0;
                    game->ui.load_scroll = 0;
                } else {
                    SDL_Event quit_event;
                    quit_event.type = SDL_QUIT;
                    SDL_PushEvent(&quit_event);
                }
                return;
            }
        }
        return;
    }

    if (game->ui.show_pause_menu) {
        if (key == SDLK_UP) {
            if (!game->ui.menu_keyboard_active) {
                game->ui.menu_keyboard_active = true;
                game->ui.menu_mouse_active = false;
                return;
            }
            game->ui.pause_menu_index = clamp_int(game->ui.pause_menu_index - 1, 0, 3);
            return;
        }
        if (key == SDLK_DOWN) {
            if (!game->ui.menu_keyboard_active) {
                game->ui.menu_keyboard_active = true;
                game->ui.menu_mouse_active = false;
                return;
            }
            game->ui.pause_menu_index = clamp_int(game->ui.pause_menu_index + 1, 0, 3);
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_KP_ENTER) {
            switch (game->ui.pause_menu_index) {
                case 0:
                    game->ui.show_pause_menu = false;
                    return;
                case 1:
                    game->ui.show_save_modal = true;
                    game->ui.save_len = 0;
                    game->ui.save_name[0] = '\0';
                    game->ui.save_menu_index = 0;
                    return;
                case 2:
                    game->ui.show_load_menu = true;
                    game->ui.load_needs_refresh = true;
                    game->ui.load_menu_focus = 0;
                    game->ui.load_scroll = 0;
                    return;
                case 3:
                    game_resign(game);
                    return;
                default:
                    return;
            }
        }
        if (key == SDLK_ESCAPE) {
            game->ui.show_pause_menu = false;
            return;
        }
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
                if (game->ui.show_pause_menu) {
                    game->ui.pause_menu_index = 0;
                }
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

