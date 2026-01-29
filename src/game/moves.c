#include "game/logic_internal.h"

#include <string.h>
#include <ctype.h>

bool in_bounds(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

SquareState *board_square_ptr(BoardState *board, int file, int rank) {
    return &board->squares[rank][file];
}

void record_capture(GameState *state, char capturer, char captured) {
    if (!state || captured == '.') return;
    bool white = game_is_white_piece(capturer);
    char *list = white ? state->white_captures : state->black_captures;
    int *len = white ? &state->white_captures_len : &state->black_captures_len;
    int cap = (int)(sizeof(state->white_captures) / sizeof(state->white_captures[0]));
    if (*len < cap) {
        list[*len] = captured;
        *len += 1;
    }
}

static bool can_piece_move_to(const GameState *state, BoardPos from, BoardPos to) {
    if (!state) return false;
    GameState *tmp = &g_logic_tmp_secondary;
    game_copy_state_minimal(tmp, state);
    game_eval_legal_moves(tmp, from.file, from.rank);
    MoveHint hint = tmp->moves.legal_moves[to.rank][to.file];
    return (hint.flags & MOVE_LEGAL) != 0;
}

void build_san(const GameState *state, BoardPos from, BoardPos to, MoveHint hint,
    char moving, char captured, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    bool is_checkers = game_mode_is(state, "checkers");
    if (is_checkers) {
        snprintf(out, cap, "%c%d-%c%d",
            (char)('a' + from.file), 8 - from.rank,
            (char)('a' + to.file), 8 - to.rank);
        return;
    }
    if (hint.flags & MOVE_CASTLE_KING) {
        snprintf(out, cap, "O-O");
    } else if (hint.flags & MOVE_CASTLE_QUEEN) {
        snprintf(out, cap, "O-O-O");
    } else {
        char from_rank = (char)('8' - from.rank);
        char to_rank = (char)('8' - to.rank);
        bool is_pawn = (moving == 'P' || moving == 'p');
        bool is_capture = (captured != '.') || (hint.flags & MOVE_EN_PASSANT);
        char piece_char = is_pawn ? '\0' : (char)toupper((unsigned char)moving);
        char disamb[3] = {0};
        if (!is_pawn && (piece_char == 'N' || piece_char == 'B' || piece_char == 'R' || piece_char == 'Q' || piece_char == 'K')) {
            bool any_other = false;
            bool same_file = false;
            bool same_rank = false;
            for (int r = 0; r < 8; ++r) {
                for (int f = 0; f < 8; ++f) {
                    if (f == from.file && r == from.rank) continue;
                    char p = game_piece_at(state, f, r);
                    if (p == moving) {
                        if (can_piece_move_to(state, (BoardPos){f, r}, to)) {
                            any_other = true;
                            if (f == from.file) same_file = true;
                            if (r == from.rank) same_rank = true;
                        }
                    }
                }
            }
            if (any_other) {
                if (same_file && same_rank) {
                    disamb[0] = (char)('a' + from.file);
                    disamb[1] = from_rank;
                    disamb[2] = '\0';
                } else if (same_file) {
                    disamb[0] = from_rank;
                    disamb[1] = '\0';
                } else if (same_rank) {
                    disamb[0] = (char)('a' + from.file);
                    disamb[1] = '\0';
                } else {
                    disamb[0] = (char)('a' + from.file);
                    disamb[1] = '\0';
                }
            }
        }
        char dest[3];
        dest[0] = (char)('a' + to.file);
        dest[1] = to_rank;
        dest[2] = '\0';
        char promo[4] = {0};
        if ((hint.flags & MOVE_PROMOTION) && hint.promotion) {
            promo[0] = '=';
            promo[1] = (char)toupper((unsigned char)hint.promotion);
            promo[2] = '\0';
        }
        if (is_pawn) {
            if (is_capture) {
                snprintf(out, cap, "%c%c%s%s", (char)('a' + from.file), 'x', dest, promo);
            } else {
                snprintf(out, cap, "%s%s", dest, promo);
            }
        } else {
            if (is_capture) {
                snprintf(out, cap, "%c%sx%s%s", piece_char, disamb, dest, promo);
            } else {
                snprintf(out, cap, "%c%s%s%s", piece_char, disamb, dest, promo);
            }
        }
    }
    GameState *tmp = &g_logic_tmp_primary;
    game_copy_state_minimal(tmp, state);
    apply_basic_move(tmp, from.file, from.rank, to.file, to.rank, hint, false);
    update_game_phase_chess(tmp);
    if (strncmp(tmp->match.phase, "checkmate", 9) == 0) {
        strncat(out, "#", cap - strlen(out) - 1);
    } else if (game_is_in_check(tmp, tmp->match.white_to_move)) {
        strncat(out, "+", cap - strlen(out) - 1);
    }
}

void record_move(GameState *state, BoardPos from, BoardPos to, char piece, char promotion, bool white_move, MoveHint hint, char captured) {
    if (!state) return;
    int cap = (int)(sizeof(state->move_history) / sizeof(state->move_history[0]));
    if (state->move_history_len >= cap) return;
    MoveRecord *rec = &state->move_history[state->move_history_len++];
    rec->from = from;
    rec->to = to;
    rec->piece = piece;
    rec->promotion = promotion;
    rec->white_move = white_move;
    rec->san[0] = '\0';
    build_san(state, from, to, hint, piece, captured, rec->san, sizeof(rec->san));
}

void apply_basic_move(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint, bool update_counters) {
    SquareState *src = board_square_ptr(&state->board_state, from_file, from_rank);
    SquareState *dst = board_square_ptr(&state->board_state, to_file, to_rank);
    char moving = src->piece;
    char captured = dst->piece;

    // En passant capture: remove the pawn behind the target square.
    if (hint.flags & MOVE_EN_PASSANT) {
        int captured_rank = to_rank + (moving == 'P' ? 1 : -1);
        SquareState *ep = board_square_ptr(&state->board_state, to_file, captured_rank);
        ep->piece = '.';
        ep->flags = 0;
    }

    src->piece = '.';
    src->flags = 0;
    dst->piece = moving;
    dst->flags = 0;

    // Promotion
    if ((hint.flags & MOVE_PROMOTION) && hint.promotion) {
        dst->piece = hint.promotion;
    }

    // Castling: move rook.
    if (hint.flags & MOVE_CASTLE_KING) {
        int rook_from_file = 7;
        int rook_to_file = 5;
        SquareState *rook_src = board_square_ptr(&state->board_state, rook_from_file, from_rank);
        SquareState *rook_dst = board_square_ptr(&state->board_state, rook_to_file, from_rank);
        rook_dst->piece = rook_src->piece;
        rook_dst->flags = 0;
        rook_src->piece = '.';
        rook_src->flags = 0;
    } else if (hint.flags & MOVE_CASTLE_QUEEN) {
        int rook_from_file = 0;
        int rook_to_file = 3;
        SquareState *rook_src = board_square_ptr(&state->board_state, rook_from_file, from_rank);
        SquareState *rook_dst = board_square_ptr(&state->board_state, rook_to_file, from_rank);
        rook_dst->piece = rook_src->piece;
        rook_dst->flags = 0;
        rook_src->piece = '.';
        rook_src->flags = 0;
    }

    // Update castling rights when king or rooks move, or rooks are captured.
    if (moving == 'K') {
        state->chess.rights.white_can_castle_kingside = false;
        state->chess.rights.white_can_castle_queenside = false;
    } else if (moving == 'k') {
        state->chess.rights.black_can_castle_kingside = false;
        state->chess.rights.black_can_castle_queenside = false;
    } else if (moving == 'R') {
        if (from_rank == 7 && from_file == 0) state->chess.rights.white_can_castle_queenside = false;
        if (from_rank == 7 && from_file == 7) state->chess.rights.white_can_castle_kingside = false;
    } else if (moving == 'r') {
        if (from_rank == 0 && from_file == 0) state->chess.rights.black_can_castle_queenside = false;
        if (from_rank == 0 && from_file == 7) state->chess.rights.black_can_castle_kingside = false;
    }
    // Capture rook on its starting square removes opponent's right.
    if (captured == 'R') {
        if (to_rank == 7 && to_file == 0) state->chess.rights.white_can_castle_queenside = false;
        if (to_rank == 7 && to_file == 7) state->chess.rights.white_can_castle_kingside = false;
    } else if (captured == 'r') {
        if (to_rank == 0 && to_file == 0) state->chess.rights.black_can_castle_queenside = false;
        if (to_rank == 0 && to_file == 7) state->chess.rights.black_can_castle_kingside = false;
    }

    // En passant target update
    state->chess.rights.en_passant_target = (BoardPos){-1, -1};
    if (moving == 'P' && from_rank == 6 && to_rank == 4) {
        state->chess.rights.en_passant_target = (BoardPos){from_file, 5};
    } else if (moving == 'p' && from_rank == 1 && to_rank == 3) {
        state->chess.rights.en_passant_target = (BoardPos){from_file, 2};
    }

    state->match.white_to_move = !state->match.white_to_move;

    if (update_counters) {
        bool pawn_move = moving == 'P' || moving == 'p';
        bool is_capture = captured != '.' || (hint.flags & MOVE_EN_PASSANT);
        state->chess.rights.halfmove_clock = (pawn_move || is_capture) ? 0 : state->chess.rights.halfmove_clock + 1;
        record_position(state);
    }
}

void game_clear_move_flags(GameState *state) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            SquareState *sq = board_square_ptr(&state->board_state, file, rank);
            sq->flags &= ~(SQUARE_LEGAL | SQUARE_HIGHLIGHT);
            state->moves.legal_moves[rank][file].flags = 0;
            state->moves.legal_moves[rank][file].promotion = '\0';
        }
    }
}

char game_piece_at(const GameState *state, int file, int rank) {
    return state->board_state.squares[rank][file].piece;
}

void init_board_chess(GameState *state) {
    static const char *start_rows[8] = {
        "rnbqkbnr",
        "pppppppp",
        "........",
        "........",
        "........",
        "........",
        "PPPPPPPP",
        "RNBQKBNR",
    };

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = start_rows[rank][file];
            SquareState *sq = board_square_ptr(&state->board_state, file, rank);
            sq->piece = piece;
            sq->flags = 0;
            state->moves.legal_moves[rank][file].flags = 0;
            state->moves.legal_moves[rank][file].promotion = '\0';
        }
    }
}

void init_board_checkers(GameState *state) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = '.';
            if ((file + rank) % 2 == 1) {
                if (rank < 3) piece = 'm';       // black men
                else if (rank > 4) piece = 'M';  // white men
            }
            SquareState *sq = board_square_ptr(&state->board_state, file, rank);
            sq->piece = piece;
            sq->flags = 0;
            state->moves.legal_moves[rank][file].flags = 0;
            state->moves.legal_moves[rank][file].promotion = '\0';
        }
    }
}

void init_board_for_mode(GameState *state) {
    if (game_mode_is(state, "checkers")) {
        init_board_checkers(state);
    } else {
        init_board_chess(state);
    }
}
