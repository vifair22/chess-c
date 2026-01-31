#include "game/logic.h"
#include "game/logic_internal.h"

#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#define GAME_SNAPSHOT_MAX 256

// Thread-local temporary state for move generation and validation
THREAD_LOCAL GameState g_logic_tmp_primary;
THREAD_LOCAL GameState g_logic_tmp_secondary;

// === Public utility functions ===

bool game_is_white_piece(char piece) {
    return piece >= 'A' && piece <= 'Z';
}

bool game_is_black_piece(char piece) {
    return piece >= 'a' && piece <= 'z';
}

bool game_is_terminal_phase(const char *phase) {
    if (!phase) return false;
    return strncmp(phase, "checkmate", 9) == 0 ||
           strncmp(phase, "stalemate", 9) == 0 ||
           strncmp(phase, "win_", 4) == 0 ||
           strncmp(phase, "draw", 4) == 0;
}

// === Internal helper functions ===

bool game_mode_is(const GameState *state, const char *mode) {
    return strncmp(state->match.mode, mode, sizeof(state->match.mode)) == 0;
}

static bool game_allows_moves_ex(const GameState *state, bool allow_ai_turn) {
    // Block further input after terminal results.
    if (game_is_terminal_phase(state->match.phase)) {
        return false;
    }
    if (!allow_ai_turn && state->ai.side != AI_SIDE_NONE) {
        bool ai_is_white = state->ai.side == AI_SIDE_WHITE;
        if (state->match.white_to_move == ai_is_white) {
            return false;
        }
    }
    return true;
}

static bool game_allows_moves(const GameState *state) {
    return game_allows_moves_ex(state, false);
}

// === Click handling ===

static void game_handle_click_chess(GameState *state, int file, int rank) {
    if ((unsigned)file >= 8 || (unsigned)rank >= 8) {
        return;
    }
    char clicked = game_piece_at(state, file, rank);
    bool clicked_white = game_is_white_piece(clicked);
    bool clicked_black = game_is_black_piece(clicked);

    bool has_selection = state->moves.selected.file >= 0 && state->moves.selected.rank >= 0;

    if (!has_selection) {
        if (clicked != '.' &&
            ((state->match.white_to_move && clicked_white) ||
             (!state->match.white_to_move && clicked_black))) {
            state->moves.selected.file = file;
            state->moves.selected.rank = rank;
            game_eval_legal_moves(state, file, rank);
            state->ui.ai_highlight_suppressed = true;
        }
        return;
    }

    // Clicking the same square toggles selection off.
    if (state->moves.selected.file == file && state->moves.selected.rank == rank) {
        state->moves.selected.file = -1;
        state->moves.selected.rank = -1;
        game_clear_move_flags(state);
        state->ui.ai_highlight_suppressed = false;
        return;
    }

    char selected_piece = game_piece_at(state, state->moves.selected.file, state->moves.selected.rank);
    bool selected_white = game_is_white_piece(selected_piece);

    // Switch selection to another friendly piece.
    if (clicked != '.' && ((selected_white && clicked_white) || (!selected_white && clicked_black))) {
        state->moves.selected.file = file;
        state->moves.selected.rank = rank;
        game_eval_legal_moves(state, file, rank);
        state->ui.ai_highlight_suppressed = true;
        return;
    }

    MoveHint hint = state->moves.legal_moves[rank][file];
    if (!(hint.flags & MOVE_LEGAL)) {
        return;
    }

    // Apply legal move with metadata (handles castles/en passant/promo).
    apply_chess_move_common(state,
        (BoardPos){state->moves.selected.file, state->moves.selected.rank},
        (BoardPos){file, rank},
        hint);
}

static void game_handle_click_checkers(GameState *state, int file, int rank) {
    if ((unsigned)file >= 8 || (unsigned)rank >= 8) {
        return;
    }
    char clicked = game_piece_at(state, file, rank);
    bool clicked_white = game_is_white_piece(clicked);
    bool clicked_black = game_is_black_piece(clicked);
    bool has_selection = state->moves.selected.file >= 0 && state->moves.selected.rank >= 0;

    if (!has_selection) {
        if (clicked != '.' &&
            ((state->match.white_to_move && clicked_white) ||
             (!state->match.white_to_move && clicked_black))) {
            state->moves.selected.file = file;
            state->moves.selected.rank = rank;
            game_eval_legal_moves_checkers(state, file, rank);
            state->ui.ai_highlight_suppressed = true;
        }
        return;
    }

    // In a multi-jump chain, the player must continue with the same piece.
    if (state->variant.checkers_must_continue) {
        if (state->moves.selected.file == file && state->moves.selected.rank == rank) {
            return;
        }
    } else {
        // Clicking the same square toggles selection off.
        if (state->moves.selected.file == file && state->moves.selected.rank == rank) {
            state->moves.selected.file = -1;
            state->moves.selected.rank = -1;
            game_clear_move_flags(state);
            state->ui.ai_highlight_suppressed = false;
            return;
        }

        // Switch selection to another friendly piece.
        if (clicked != '.' &&
            ((state->match.white_to_move && clicked_white) ||
             (!state->match.white_to_move && clicked_black))) {
            state->moves.selected.file = file;
            state->moves.selected.rank = rank;
            game_eval_legal_moves_checkers(state, file, rank);
            state->ui.ai_highlight_suppressed = true;
            return;
        }
    }

    MoveHint hint = state->moves.legal_moves[rank][file];
    if (!(hint.flags & MOVE_LEGAL)) {
        return;
    }

    apply_checkers_move_common(state,
        state->moves.selected.file, state->moves.selected.rank,
        file, rank,
        hint,
        !state->variant.checkers_must_continue);
}

void game_handle_click(GameState *state, int file, int rank) {
    if (file < 0 || file >= 8 || rank < 0 || rank >= 8) {
        return;
    }

    if (!game_allows_moves(state)) {
        return;
    }

    if (game_mode_is(state, "checkers")) {
        game_handle_click_checkers(state, file, rank);
    } else {
        game_handle_click_chess(state, file, rank);
    }
}

// === Move application ===

static void log_move_reject(const GameState *state, BoardPos from, BoardPos to, MoveHint hint,
    const char *context, const char *reason) {
    if (!state) {
        SDL_Log("Move rejected (%s): %s (state=NULL) from %d,%d to %d,%d (flags=%u promo=%c)",
            context ? context : "unknown", reason ? reason : "unknown",
            from.file, from.rank, to.file, to.rank, hint.flags,
            hint.promotion ? hint.promotion : '-');
        return;
    }
    SDL_Log("Move rejected (%s): %s from %d,%d to %d,%d (flags=%u promo=%c) phase=%s turn=%d white_to_move=%d",
        context ? context : "unknown", reason ? reason : "unknown",
        from.file, from.rank, to.file, to.rank, hint.flags,
        hint.promotion ? hint.promotion : '-', state->match.phase, state->match.turn_number,
        state->match.white_to_move ? 1 : 0);
}

static bool game_apply_move_internal(GameState *state, BoardPos from, BoardPos to, MoveHint hint,
    bool allow_ai_turn, const char *context) {
    if (!state) {
        log_move_reject(state, from, to, hint, context, "null state");
        return false;
    }
    if (from.file < 0 || from.file >= 8 || from.rank < 0 || from.rank >= 8) {
        log_move_reject(state, from, to, hint, context, "from out of bounds");
        return false;
    }
    if (to.file < 0 || to.file >= 8 || to.rank < 0 || to.rank >= 8) {
        log_move_reject(state, from, to, hint, context, "to out of bounds");
        return false;
    }
    if (!game_allows_moves_ex(state, allow_ai_turn)) {
        log_move_reject(state, from, to, hint, context, "input blocked");
        return false;
    }
    char promo_override = ((hint.flags & MOVE_PROMOTION) && hint.promotion) ? hint.promotion : '\0';
    if (game_mode_is(state, "checkers")) {
        if (!(hint.flags & MOVE_LEGAL)) {
            game_eval_legal_moves(state, from.file, from.rank);
            hint = state->moves.legal_moves[to.rank][to.file];
            if (!(hint.flags & MOVE_LEGAL)) {
                game_clear_move_flags(state);
                log_move_reject(state, from, to, hint, context, "illegal move");
                return false;
            }
        }
        if (promo_override) {
            hint.flags |= MOVE_PROMOTION;
            hint.promotion = promo_override;
        }
        apply_checkers_move_common(state,
            from.file, from.rank,
            to.file, to.rank,
            hint,
            !state->variant.checkers_must_continue);
        if (!allow_ai_turn) {
            state->ui.ai_highlight_active = false;
            state->ui.ai_highlight_suppressed = false;
        }
        return true;
    }

    if (!(hint.flags & MOVE_LEGAL)) {
        game_eval_legal_moves(state, from.file, from.rank);
        hint = state->moves.legal_moves[to.rank][to.file];
        if (!(hint.flags & MOVE_LEGAL)) {
            game_clear_move_flags(state);
            log_move_reject(state, from, to, hint, context, "illegal move");
            return false;
        }
    }
    if (promo_override) {
        hint.flags |= MOVE_PROMOTION;
        hint.promotion = promo_override;
    }

    apply_chess_move_common(state, from, to, hint);
    if (!allow_ai_turn) {
        state->ui.ai_highlight_active = false;
        state->ui.ai_highlight_suppressed = false;
    }
    return true;
}

bool game_apply_move(GameState *state, BoardPos from, BoardPos to, MoveHint hint) {
    return game_apply_move_internal(state, from, to, hint, false, "game");
}

bool game_apply_move_ai(GameState *state, BoardPos from, BoardPos to, MoveHint hint) {
    bool ok = game_apply_move_internal(state, from, to, hint, true, "ai");
    if (!ok) {
        SDL_Log("AI attempted an invalid move: from %d,%d to %d,%d (flags=%u promo=%c)",
            from.file, from.rank, to.file, to.rank, hint.flags,
            hint.promotion ? hint.promotion : '-');
        SDL_assert(!"AI made an invalid move");
    }
    if (ok) {
        state->ui.ai_highlight_active = true;
        state->ui.ai_highlight_suppressed = false;
        state->ui.ai_highlight_from = from;
        state->ui.ai_highlight_to = to;
    }
    return ok;
}

// === Move evaluation dispatcher ===

void game_eval_legal_moves(GameState *state, int file, int rank) {
    if (game_mode_is(state, "checkers")) {
        game_eval_legal_moves_checkers(state, file, rank);
    } else {
        game_eval_legal_moves_chess(state, file, rank);
    }
}

// === Game lifecycle ===

bool game_init(GameState *state) {
    state->mutex = SDL_CreateMutex();
    if (!state->mutex) {
        return false;
    }

    if (state->ai.depth <= 0) {
        state->ai.depth = 2;
    }
    if (state->ai.side < AI_SIDE_NONE || state->ai.side > AI_SIDE_BLACK) {
        state->ai.side = AI_SIDE_NONE;
    }
    if (!state->snapshots) {
        state->snapshots = (GameSnapshot *)calloc(GAME_SNAPSHOT_MAX, sizeof(GameSnapshot));
        state->snapshot_cap = state->snapshots ? GAME_SNAPSHOT_MAX : 0;
        state->snapshot_len = 0;
    }

    game_reset(state, state->match.mode[0] ? state->match.mode : "chess");
    state->ui.show_main_menu = true;
    state->ui.show_pause_menu = false;

    return true;
}

void game_destroy(GameState *state) {
    if (state && state->mutex) {
        SDL_DestroyMutex(state->mutex);
        state->mutex = NULL;
    }
    if (state && state->snapshots) {
        free(state->snapshots);
        state->snapshots = NULL;
        state->snapshot_len = 0;
        state->snapshot_cap = 0;
    }
}

void game_full_reset(GameState *state) {
    if (!state) return;
    uint32_t reset_counter = state->ui.full_reset_counter + 1;
    SDL_mutex *mutex = state->mutex;
    GameSnapshot *snapshots = state->snapshots;
    int snapshot_cap = state->snapshot_cap;
    memset(state, 0, sizeof(*state));
    state->mutex = mutex;
    state->snapshots = snapshots;
    state->snapshot_cap = snapshot_cap;
    if (!state->snapshots) {
        state->snapshots = (GameSnapshot *)calloc(GAME_SNAPSHOT_MAX, sizeof(GameSnapshot));
        state->snapshot_cap = state->snapshots ? GAME_SNAPSHOT_MAX : 0;
        state->snapshot_len = 0;
    }
    state->ai.depth = 2;
    state->ai.side = AI_SIDE_NONE;
    game_reset(state, "chess");
    state->ui.show_main_menu = true;
    state->ui.show_pause_menu = false;
    state->ui.show_new_menu = false;
    state->ui.full_reset_counter = reset_counter;
}

void game_resign(GameState *state) {
    if (!state || game_is_terminal_phase(state->match.phase)) {
        return;
    }
    const char *phase = state->match.white_to_move ? "win_black_resign" : "win_white_resign";
    strncpy(state->match.phase, phase, sizeof(state->match.phase) - 1);
    state->match.phase[sizeof(state->match.phase) - 1] = '\0';
    state->ui.show_pause_menu = false;
    state->ui.show_main_menu = false;
    state->moves.selected.file = -1;
    state->moves.selected.rank = -1;
}

void game_reset(GameState *state, const char *mode) {
    if (!mode || mode[0] == '\0') {
        mode = "chess";
    }
    strncpy(state->match.mode, mode, sizeof(state->match.mode) - 1);
    state->match.mode[sizeof(state->match.mode) - 1] = '\0';

    init_board_for_mode(state);

    state->match.white_to_move = game_mode_is(state, "checkers") ? false : true;
    state->moves.selected.file = -1;
    state->moves.selected.rank = -1;
    state->moves.hover_pos = (BoardPos){-1, -1};

    state->match.turn_number = 0;
    state->match.game_time.total_seconds = 0.0;
    strncpy(state->match.phase, "play", sizeof(state->match.phase) - 1);
    state->match.phase[sizeof(state->match.phase) - 1] = '\0';
    state->last_move.start = (BoardPos){-1, -1};
    state->last_move.end = (BoardPos){-1, -1};
    state->chess.rights.white_can_castle_kingside = true;
    state->chess.rights.white_can_castle_queenside = true;
    state->chess.rights.black_can_castle_kingside = true;
    state->chess.rights.black_can_castle_queenside = true;
    state->chess.rights.en_passant_target = (BoardPos){-1, -1};
    state->variant.checkers_must_continue = false;
    state->perf.smoothed_frame_ms = 0.0;
    state->perf.smoothed_fps = 0.0;
    state->perf.accum_time = 0.0;
    state->perf.accum_frames = 0;
    state->ui.show_debug = true;
    state->variant.promotion_choice_white = 'Q';
    state->variant.promotion_choice_black = 'q';
    state->chess.rights.halfmove_clock = 0;
    state->chess.history.len = 0;
    state->snapshot_len = 0;
    state->ui.undo_used = false;
    state->ui.show_save_modal = false;
    state->ui.show_load_menu = false;
    state->ui.show_load_confirm = false;
    state->ui.load_needs_refresh = false;
    state->ui.load_is_replay = false;
    state->ui.replay_saved = false;
    state->ui.replay_paused = false;
    state->ui.win_menu_index = 0;
    state->ui.main_menu_index = 0;
    state->ui.new_menu_index = 0;
    state->ui.pause_menu_index = 0;
    state->ui.save_menu_index = 0;
    state->ui.load_menu_focus = 0;
    state->ui.load_confirm_index = 0;
    state->ui.menu_keyboard_active = false;
    state->ui.menu_mouse_active = true;
    state->ui.save_is_replay = false;
    state->ui.save_len = 0;
    state->ui.save_name[0] = '\0';
    state->ui.load_count = 0;
    state->ui.load_scroll = 0;
    state->ui.load_selected = -1;
    state->ui.load_scroll_dragging = false;
    state->ui.load_scroll_drag_offset = 0;
    state->ui.turn_log_scroll = 0;
    state->ui.turn_log_max_scroll = 0;
    state->ui.replay_mode_active = false;
    state->ui.replay_request_pending = false;
    state->ui.replay_request[0] = '\0';
    state->ui.ai_highlight_active = false;
    state->ui.ai_highlight_suppressed = false;
    state->ui.ai_highlight_from = (BoardPos){-1, -1};
    state->ui.ai_highlight_to = (BoardPos){-1, -1};
    state->autosave_last_turn = -1;
    state->autosave_terminal_saved = false;
    state->white_captures_len = 0;
    state->black_captures_len = 0;
    memset(state->white_captures, 0, sizeof(state->white_captures));
    memset(state->black_captures, 0, sizeof(state->black_captures));
    state->move_history_len = 0;
    memset(state->move_history, 0, sizeof(state->move_history));

    game_clear_move_flags(state);
    reset_position_history(state);
}

void game_recompute_phase(GameState *state) {
    if (!state) return;
    if (game_mode_is(state, "checkers")) {
        update_game_phase_checkers(state);
    } else {
        update_game_phase_chess(state);
    }
}
