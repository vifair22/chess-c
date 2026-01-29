#include "game/logic_internal.h"

#include <string.h>

// Forward declarations for internal helpers
static void add_move_checkers(GameState *state, int from_file, int from_rank, int to_file, int to_rank, bool is_capture);
static void checkers_gen_moves_for_piece(GameState *state, int file, int rank, bool force_capture);
static bool checkers_piece_has_simple_step(const GameState *state, int file, int rank, char piece);
static bool is_empty(const GameState *state, int file, int rank);
static bool is_opponent_piece(const GameState *state, int file, int rank, bool white);
static void apply_checkers_move(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint);

bool checkers_is_king(char piece) {
    return piece == 'K' || piece == 'k';
}

bool checkers_can_capture_from(const GameState *state, int file, int rank, char piece) {
    bool white = game_is_white_piece(piece);
    int dir = white ? -1 : 1;
    const int dirs_king[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const int dirs_man[2][2] = {{1, dir}, {-1, dir}};
    const int (*dirs)[2] = checkers_is_king(piece) ? dirs_king : dirs_man;
    int dir_count = checkers_is_king(piece) ? 4 : 2;

    for (int i = 0; i < dir_count; ++i) {
        int mid_file = file + dirs[i][0];
        int mid_rank = rank + dirs[i][1];
        int landing_file = file + 2 * dirs[i][0];
        int landing_rank = rank + 2 * dirs[i][1];
        if (!in_bounds(mid_file, mid_rank) || !in_bounds(landing_file, landing_rank)) continue;
        char mid_piece = game_piece_at(state, mid_file, mid_rank);
        if (mid_piece == '.') continue;
        if (white ? game_is_white_piece(mid_piece) : game_is_black_piece(mid_piece)) continue;
        if (game_piece_at(state, landing_file, landing_rank) == '.') {
            return true;
        }
    }
    return false;
}

bool checkers_player_has_capture(const GameState *state, bool white) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char p = game_piece_at(state, file, rank);
            if (p == '.') continue;
            if (white ? !game_is_white_piece(p) : !game_is_black_piece(p)) continue;
            if (checkers_can_capture_from(state, file, rank, p)) {
                return true;
            }
        }
    }
    return false;
}

static bool checkers_piece_has_simple_step(const GameState *state, int file, int rank, char piece) {
    bool white = game_is_white_piece(piece);
    int dir = white ? -1 : 1;
    const int dirs_king[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const int dirs_man[2][2] = {{1, dir}, {-1, dir}};
    const int (*dirs)[2] = checkers_is_king(piece) ? dirs_king : dirs_man;
    int dir_count = checkers_is_king(piece) ? 4 : 2;

    for (int i = 0; i < dir_count; ++i) {
        int tf = file + dirs[i][0];
        int tr = rank + dirs[i][1];
        if (in_bounds(tf, tr) && game_piece_at(state, tf, tr) == '.') {
            return true;
        }
    }
    return false;
}

bool checkers_player_has_any_move(const GameState *state, bool white) {
    bool has_capture = checkers_player_has_capture(state, white);
    if (has_capture) {
        return true; // capture exists, so a move exists
    }
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char p = game_piece_at(state, file, rank);
            if (p == '.') continue;
            if (white ? !game_is_white_piece(p) : !game_is_black_piece(p)) continue;
            if (checkers_piece_has_simple_step(state, file, rank, p)) {
                return true;
            }
        }
    }
    return false;
}

void update_game_phase_checkers(GameState *state) {
    int white_pieces = 0;
    int black_pieces = 0;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char p = game_piece_at(state, file, rank);
            if (p == '.') continue;
            if (game_is_white_piece(p)) ++white_pieces;
            else ++black_pieces;
        }
    }

    if (white_pieces == 0) {
        strncpy(state->match.phase, "win_black", sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
        return;
    }
    if (black_pieces == 0) {
        strncpy(state->match.phase, "win_white", sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
        return;
    }

    bool to_move_white = state->match.white_to_move;
    bool can_move = checkers_player_has_any_move(state, to_move_white);
    if (!can_move) {
        const char *phase = to_move_white ? "win_black" : "win_white";
        strncpy(state->match.phase, phase, sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
    } else {
        strncpy(state->match.phase, "play", sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
    }
}

static void add_move_checkers(GameState *state, int from_file, int from_rank, int to_file, int to_rank, bool is_capture) {
    if (!in_bounds(to_file, to_rank)) {
        return;
    }
    MoveHint hint = {0};
    if (is_capture) {
        hint.flags |= MOVE_CAPTURE;
    }
    char moving = game_piece_at(state, from_file, from_rank);
    bool white = game_is_white_piece(moving);
    int promo_rank = white ? 0 : 7;
    if (!checkers_is_king(moving) && to_rank == promo_rank) {
        hint.flags |= MOVE_PROMOTION;
        hint.promotion = white ? 'K' : 'k';
    }

    SquareState *dst = board_square_ptr(&state->board_state, to_file, to_rank);
    dst->flags |= SQUARE_LEGAL;
    state->moves.legal_moves[to_rank][to_file] = hint;
    state->moves.legal_moves[to_rank][to_file].flags |= MOVE_LEGAL;
}

static void checkers_gen_moves_for_piece(GameState *state, int file, int rank, bool force_capture) {
    char piece = game_piece_at(state, file, rank);
    if (piece == '.') return;
    bool white = game_is_white_piece(piece);
    int dir = white ? -1 : 1;
    const int dirs_king[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const int dirs_man[2][2] = {{1, dir}, {-1, dir}};
    const int (*dirs)[2] = checkers_is_king(piece) ? dirs_king : dirs_man;
    int dir_count = checkers_is_king(piece) ? 4 : 2;

    bool added_capture = false;
    for (int i = 0; i < dir_count; ++i) {
        int mid_file = file + dirs[i][0];
        int mid_rank = rank + dirs[i][1];
        int landing_file = file + 2 * dirs[i][0];
        int landing_rank = rank + 2 * dirs[i][1];
        if (!in_bounds(mid_file, mid_rank) || !in_bounds(landing_file, landing_rank)) continue;
        if (!is_opponent_piece(state, mid_file, mid_rank, white)) continue;
        if (!is_empty(state, landing_file, landing_rank)) continue;
        add_move_checkers(state, file, rank, landing_file, landing_rank, true);
        added_capture = true;
    }

    if (force_capture && added_capture) {
        return;
    }
    if (force_capture && !added_capture) {
        return; // no legal non-capture when captures exist elsewhere
    }

    for (int i = 0; i < dir_count; ++i) {
        int tf = file + dirs[i][0];
        int tr = rank + dirs[i][1];
        if (!in_bounds(tf, tr)) continue;
        if (!is_empty(state, tf, tr)) continue;
        add_move_checkers(state, file, rank, tf, tr, false);
    }
}

void game_eval_legal_moves_checkers(GameState *state, int file, int rank) {
    if (!state) return;
    game_clear_move_flags(state);
    if (!in_bounds(file, rank)) return;
    char piece = game_piece_at(state, file, rank);
    if (piece == '.') return;
    bool white = game_is_white_piece(piece);
    if ((white && !state->match.white_to_move) || (!white && state->match.white_to_move)) {
        return;
    }
    if (state->variant.checkers_must_continue &&
        (file != state->moves.selected.file || rank != state->moves.selected.rank)) {
        return; // must continue jumping with the same piece
    }

    bool force_capture = checkers_player_has_capture(state, state->match.white_to_move);
    checkers_gen_moves_for_piece(state, file, rank, force_capture);
}

static void apply_checkers_move(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint) {
    SquareState *src = board_square_ptr(&state->board_state, from_file, from_rank);
    char moving = src->piece;
    bool capture = (hint.flags & MOVE_CAPTURE) != 0;
    if (capture) {
        int cap_file = (from_file + to_file) / 2;
        int cap_rank = (from_rank + to_rank) / 2;
        SquareState *cap = board_square_ptr(&state->board_state, cap_file, cap_rank);
        char captured = cap->piece;
        record_capture(state, moving, captured);
        cap->piece = '.';
        cap->flags = 0;
    }

    src->piece = '.';
    src->flags = 0;

    char placed = moving;
    bool became_king = false;
    if ((hint.flags & MOVE_PROMOTION) && hint.promotion) {
        placed = hint.promotion;
        became_king = !checkers_is_king(moving);
    }
    SquareState *dst = board_square_ptr(&state->board_state, to_file, to_rank);
    dst->piece = placed;
    dst->flags = 0;

    state->last_move.start = (BoardPos){from_file, from_rank};
    state->last_move.end = (BoardPos){to_file, to_rank};
    state->chess.rights.en_passant_target = (BoardPos){-1, -1};

    bool more_captures = capture && checkers_can_capture_from(state, to_file, to_rank, placed);
    if (more_captures) {
        state->moves.selected.file = to_file;
        state->moves.selected.rank = to_rank;
        state->variant.checkers_must_continue = true;
        game_clear_move_flags(state);
        game_eval_legal_moves_checkers(state, to_file, to_rank);
    } else {
        state->variant.checkers_must_continue = false;
        state->match.white_to_move = !state->match.white_to_move;
        game_clear_move_flags(state);
        state->moves.selected.file = -1;
        state->moves.selected.rank = -1;
        state->match.turn_number += 1;
        update_game_phase_checkers(state);
    }
}

void apply_checkers_move_common(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint, bool snapshot) {
    if (snapshot) {
        snapshot_push(state);
        state->ui.undo_used = false;
    }
    char moving = game_piece_at(state, from_file, from_rank);
    record_move(state, (BoardPos){from_file, from_rank}, (BoardPos){to_file, to_rank}, moving, '\0', state->match.white_to_move, hint, '\0');
    apply_checkers_move(state, from_file, from_rank, to_file, to_rank, hint);
}

static bool is_empty(const GameState *state, int file, int rank) {
    return in_bounds(file, rank) && game_piece_at(state, file, rank) == '.';
}

static bool is_opponent_piece(const GameState *state, int file, int rank, bool white) {
    char p = game_piece_at(state, file, rank);
    if (p == '.') return false;
    return white ? game_is_black_piece(p) : game_is_white_piece(p);
}
