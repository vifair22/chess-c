#include "game/logic_internal.h"

#include <string.h>
#include <SDL.h>

// Forward declarations for internal helpers
static void add_move_if_legal(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint);
static bool is_empty(const GameState *state, int file, int rank);
static bool is_opponent_piece(const GameState *state, int file, int rank, bool white);
static void gen_pawn_moves(GameState *state, int file, int rank, bool white);
static void gen_knight_moves(GameState *state, int file, int rank, bool white);
static void gen_sliding(GameState *state, int file, int rank, bool white, const int dirs[][2], int dir_count);
static void gen_king_moves(GameState *state, int file, int rank, bool white);

BoardPos find_king(const GameState *state, bool white) {
    char king = white ? 'K' : 'k';
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            if (game_piece_at(state, file, rank) == king) {
                return (BoardPos){file, rank};
            }
        }
    }
    return (BoardPos){-1, -1};
}

bool game_square_attacked_by(const GameState *state, int file, int rank, bool by_white) {
    // Pawn attacks
    int pawn_dir = by_white ? 1 : -1;
    int pawn_rank = rank + pawn_dir;
    if (in_bounds(file - 1, pawn_rank)) {
        char p = game_piece_at(state, file - 1, pawn_rank);
        if ((by_white && p == 'P') || (!by_white && p == 'p')) {
            return true;
        }
    }
    if (in_bounds(file + 1, pawn_rank)) {
        char p = game_piece_at(state, file + 1, pawn_rank);
        if ((by_white && p == 'P') || (!by_white && p == 'p')) {
            return true;
        }
    }

    // Knights
    static const int knight_offsets[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
        {1, -2}, {2, -1}, {-1, -2}, {-2, -1},
    };
    for (int i = 0; i < 8; ++i) {
        int f = file + knight_offsets[i][0];
        int r = rank + knight_offsets[i][1];
        if (!in_bounds(f, r)) continue;
        char p = game_piece_at(state, f, r);
        if ((by_white && p == 'N') || (!by_white && p == 'n')) {
            return true;
        }
    }

    // Sliding: bishops/queens diagonals
    static const int diag_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = file + diag_dirs[d][0];
        int r = rank + diag_dirs[d][1];
        while (in_bounds(f, r)) {
            char p = game_piece_at(state, f, r);
            if (p != '.') {
                if (by_white && (p == 'B' || p == 'Q')) return true;
                if (!by_white && (p == 'b' || p == 'q')) return true;
                break;
            }
            f += diag_dirs[d][0];
            r += diag_dirs[d][1];
        }
    }

    // Sliding: rooks/queens orthogonal
    static const int ortho_dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = file + ortho_dirs[d][0];
        int r = rank + ortho_dirs[d][1];
        while (in_bounds(f, r)) {
            char p = game_piece_at(state, f, r);
            if (p != '.') {
                if (by_white && (p == 'R' || p == 'Q')) return true;
                if (!by_white && (p == 'r' || p == 'q')) return true;
                break;
            }
            f += ortho_dirs[d][0];
            r += ortho_dirs[d][1];
        }
    }

    // King adjacency
    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            int f = file + df;
            int r = rank + dr;
            if (!in_bounds(f, r)) continue;
            char p = game_piece_at(state, f, r);
            if ((by_white && p == 'K') || (!by_white && p == 'k')) {
                return true;
            }
        }
    }
    return false;
}

bool game_is_in_check(const GameState *state, bool white) {
    BoardPos king = find_king(state, white);
    if (king.file == -1 || king.rank == -1) {
        return false;
    }
    return game_square_attacked_by(state, king.file, king.rank, !white);
}

bool mark_check_path(GameState *state, int kf, int kr, bool by_white) {
    // Clear existing check markers.
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            state->board_state.squares[r][f].flags &= ~SQUARE_CHECK;
        }
    }

    // Pawn checks
    int pawn_dir = by_white ? 1 : -1;
    int pr = kr + pawn_dir;
    for (int df = -1; df <= 1; df += 2) {
        int pf = kf + df;
        if (!in_bounds(pf, pr)) continue;
        char p = game_piece_at(state, pf, pr);
        if ((by_white && p == 'P') || (!by_white && p == 'p')) {
            state->board_state.squares[kr][kf].flags |= SQUARE_CHECK;
            state->board_state.squares[pr][pf].flags |= SQUARE_CHECK;
            return true;
        }
    }

    // Knights
    static const int k_offsets[8][2] = {{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}};
    for (int i = 0; i < 8; ++i) {
        int nf = kf + k_offsets[i][0];
        int nr = kr + k_offsets[i][1];
        if (!in_bounds(nf, nr)) continue;
        char p = game_piece_at(state, nf, nr);
        if ((by_white && p == 'N') || (!by_white && p == 'n')) {
            state->board_state.squares[kr][kf].flags |= SQUARE_CHECK;
            state->board_state.squares[nr][nf].flags |= SQUARE_CHECK;
            return true;
        }
    }

    // King adjacency (rare double king contact)
    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            int tf = kf + df;
            int tr = kr + dr;
            if (!in_bounds(tf, tr)) continue;
            char p = game_piece_at(state, tf, tr);
            if ((by_white && p == 'K') || (!by_white && p == 'k')) {
                state->board_state.squares[kr][kf].flags |= SQUARE_CHECK;
                state->board_state.squares[tr][tf].flags |= SQUARE_CHECK;
                return true;
            }
        }
    }

    // Sliding checks: bishops/queens (diagonals), rooks/queens (orthogonal)
    static const int diag_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = kf + diag_dirs[d][0];
        int r = kr + diag_dirs[d][1];
        while (in_bounds(f, r)) {
            char p = game_piece_at(state, f, r);
            if (p != '.') {
                if ((by_white && (p == 'B' || p == 'Q')) || (!by_white && (p == 'b' || p == 'q'))) {
                    // mark path from king to attacker inclusive
                    int pf = kf;
                    int pr2 = kr;
                    while (pf != f || pr2 != r) {
                        state->board_state.squares[pr2][pf].flags |= SQUARE_CHECK;
                        pf += diag_dirs[d][0];
                        pr2 += diag_dirs[d][1];
                    }
                    state->board_state.squares[r][f].flags |= SQUARE_CHECK;
                    return true;
                }
                break;
            }
            f += diag_dirs[d][0];
            r += diag_dirs[d][1];
        }
    }

    static const int ortho_dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = kf + ortho_dirs[d][0];
        int r = kr + ortho_dirs[d][1];
        while (in_bounds(f, r)) {
            char p = game_piece_at(state, f, r);
            if (p != '.') {
                if ((by_white && (p == 'R' || p == 'Q')) || (!by_white && (p == 'r' || p == 'q'))) {
                    int pf = kf;
                    int pr2 = kr;
                    while (pf != f || pr2 != r) {
                        state->board_state.squares[pr2][pf].flags |= SQUARE_CHECK;
                        pf += ortho_dirs[d][0];
                        pr2 += ortho_dirs[d][1];
                    }
                    state->board_state.squares[r][f].flags |= SQUARE_CHECK;
                    return true;
                }
                break;
            }
            f += ortho_dirs[d][0];
            r += ortho_dirs[d][1];
        }
    }

    return false;
}

bool has_any_legal_move(const GameState *state, bool white_to_move) {
    GameState *tmp = &g_logic_tmp_primary;
    game_copy_state_minimal(tmp, state);
    tmp->match.white_to_move = white_to_move;

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = game_piece_at(tmp, file, rank);
            if (piece == '.') continue;
            if (white_to_move ? !game_is_white_piece(piece) : !game_is_black_piece(piece)) {
                continue;
            }
            game_eval_legal_moves(tmp, file, rank);
            for (int tr = 0; tr < 8; ++tr) {
                for (int tf = 0; tf < 8; ++tf) {
                    if (tmp->moves.legal_moves[tr][tf].flags & MOVE_LEGAL) {
                        return true;
                    }
                }
            }
            game_clear_move_flags(tmp);
        }
    }
    return false;
}

void update_game_phase_chess(GameState *state) {
    bool to_move_white = state->match.white_to_move;
    bool check = game_is_in_check(state, to_move_white);
    bool can_move = has_any_legal_move(state, to_move_white);

    // Clear existing check markers.
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            state->board_state.squares[r][f].flags &= ~SQUARE_CHECK;
        }
    }

    if (!can_move) {
        const char *phase = check ? "checkmate" : "stalemate";
        strncpy(state->match.phase, phase, sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
        if (check) {
            BoardPos king = find_king(state, to_move_white);
            if (king.file >= 0 && king.rank >= 0) {
                mark_check_path(state, king.file, king.rank, !to_move_white);
            }
        }
        return;
    }

    bool draw_insufficient = is_insufficient_material(state);
    bool draw_threefold = repetition_count(state) >= 3;
    bool draw_fifty = state->chess.rights.halfmove_clock >= 100;
    if (draw_insufficient) {
        strncpy(state->match.phase, "draw_insufficient", sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
        return;
    }
    if (draw_threefold) {
        strncpy(state->match.phase, "draw_threefold", sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
        return;
    }
    if (draw_fifty) {
        strncpy(state->match.phase, "draw_fifty", sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
        return;
    }

    const char *phase = check ? "check" : "play";
    strncpy(state->match.phase, phase, sizeof(state->match.phase) - 1);
    state->match.phase[sizeof(state->match.phase) - 1] = '\0';
    if (check) {
        BoardPos king = find_king(state, to_move_white);
        if (king.file >= 0 && king.rank >= 0) {
            mark_check_path(state, king.file, king.rank, !to_move_white);
        }
    }
}

static void add_move_if_legal(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint) {
    if (!in_bounds(to_file, to_rank)) {
        return;
    }
    bool moving_white = game_is_white_piece(game_piece_at(state, from_file, from_rank));
    char target_piece = game_piece_at(state, to_file, to_rank);
    if (target_piece == (moving_white ? 'k' : 'K')) {
        return; // cannot capture the enemy king
    }
    GameState *tmp = &g_logic_tmp_secondary;
    game_copy_state_minimal(tmp, state);
    apply_basic_move(tmp, from_file, from_rank, to_file, to_rank, hint, false);
    BoardPos king = find_king(tmp, moving_white);
    if (king.file == -1 || game_square_attacked_by(tmp, king.file, king.rank, !moving_white)) {
        return; // leaves or puts own king in check
    }

    SquareState *dst = board_square_ptr(&state->board_state, to_file, to_rank);
    dst->flags |= SQUARE_LEGAL;
    state->moves.legal_moves[to_rank][to_file] = hint;
    state->moves.legal_moves[to_rank][to_file].flags |= MOVE_LEGAL;
}

static bool is_empty(const GameState *state, int file, int rank) {
    return in_bounds(file, rank) && game_piece_at(state, file, rank) == '.';
}

static bool is_opponent_piece(const GameState *state, int file, int rank, bool white) {
    char p = game_piece_at(state, file, rank);
    if (p == '.') return false;
    return white ? game_is_black_piece(p) : game_is_white_piece(p);
}

static void gen_pawn_moves(GameState *state, int file, int rank, bool white) {
    int dir = white ? -1 : 1;
    int start_rank = white ? 6 : 1;
    int promo_rank = white ? 0 : 7;
    char promo_piece = white ? state->variant.promotion_choice_white : state->variant.promotion_choice_black;
    if (promo_piece == 0) {
        promo_piece = white ? 'Q' : 'q';
    }
    int one_rank = rank + dir;
    if (is_empty(state, file, one_rank)) {
        MoveHint hint = {0};
        if (one_rank == promo_rank) {
            hint.flags |= MOVE_PROMOTION;
            hint.promotion = promo_piece;
        }
        add_move_if_legal(state, file, rank, file, one_rank, hint);
        int two_rank = rank + 2 * dir;
        if (rank == start_rank && is_empty(state, file, two_rank)) {
            MoveHint h2 = {0};
            add_move_if_legal(state, file, rank, file, two_rank, h2);
        }
    }
    for (int df = -1; df <= 1; df += 2) {
        int tf = file + df;
        int tr = rank + dir;
        if (in_bounds(tf, tr) && is_opponent_piece(state, tf, tr, white)) {
            MoveHint hint = {0};
            if (tr == promo_rank) {
                hint.flags |= MOVE_PROMOTION;
                hint.promotion = promo_piece;
            }
            add_move_if_legal(state, file, rank, tf, tr, hint);
        }
    }
    // En passant
    if (state->chess.rights.en_passant_target.file != -1) {
        if (state->chess.rights.en_passant_target.rank == rank + dir &&
            (state->chess.rights.en_passant_target.file == file - 1 || state->chess.rights.en_passant_target.file == file + 1)) {
            MoveHint hint = {.flags = MOVE_EN_PASSANT};
            add_move_if_legal(state, file, rank, state->chess.rights.en_passant_target.file, state->chess.rights.en_passant_target.rank, hint);
        }
    }
}

static void gen_knight_moves(GameState *state, int file, int rank, bool white) {
    static const int offsets[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
        {1, -2}, {2, -1}, {-1, -2}, {-2, -1},
    };
    for (int i = 0; i < 8; ++i) {
        int tf = file + offsets[i][0];
        int tr = rank + offsets[i][1];
        if (!in_bounds(tf, tr)) continue;
        char target = game_piece_at(state, tf, tr);
        if (target != '.' && ((white && game_is_white_piece(target)) || (!white && game_is_black_piece(target)))) continue;
        MoveHint hint = {0};
        add_move_if_legal(state, file, rank, tf, tr, hint);
    }
}

static void gen_sliding(GameState *state, int file, int rank, bool white, const int dirs[][2], int dir_count) {
    for (int d = 0; d < dir_count; ++d) {
        int df = dirs[d][0];
        int dr = dirs[d][1];
        int tf = file + df;
        int tr = rank + dr;
        while (in_bounds(tf, tr)) {
            char target = game_piece_at(state, tf, tr);
            if (target != '.') {
                if ((white && game_is_white_piece(target)) || (!white && game_is_black_piece(target))) {
                    break;
                }
                MoveHint hint = {0};
                add_move_if_legal(state, file, rank, tf, tr, hint);
                break;
            }
            MoveHint hint = {0};
            add_move_if_legal(state, file, rank, tf, tr, hint);
            tf += df;
            tr += dr;
        }
    }
}

static void gen_king_moves(GameState *state, int file, int rank, bool white) {
    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            int tf = file + df;
            int tr = rank + dr;
            if (!in_bounds(tf, tr)) continue;
            char target = game_piece_at(state, tf, tr);
            if (target != '.' && ((white && game_is_white_piece(target)) || (!white && game_is_black_piece(target)))) continue;
            MoveHint hint = {0};
            add_move_if_legal(state, file, rank, tf, tr, hint);
        }
    }

    // Castling
    bool in_check = game_square_attacked_by(state, file, rank, !white);
    if (in_check) return;
    int back_rank = white ? 7 : 0;
    char rook_k = game_piece_at(state, 7, back_rank);
    char rook_q = game_piece_at(state, 0, back_rank);
    bool kingside_rook_ok = white ? (rook_k == 'R') : (rook_k == 'r');
    bool queenside_rook_ok = white ? (rook_q == 'R') : (rook_q == 'r');
    if ((white ? state->chess.rights.white_can_castle_kingside : state->chess.rights.black_can_castle_kingside) && kingside_rook_ok) {
        if (game_piece_at(state, 5, back_rank) == '.' && game_piece_at(state, 6, back_rank) == '.') {
            if (!game_square_attacked_by(state, 5, back_rank, !white) && !game_square_attacked_by(state, 6, back_rank, !white)) {
                MoveHint hint = {.flags = MOVE_CASTLE_KING};
                add_move_if_legal(state, file, rank, 6, back_rank, hint);
            }
        }
    }
    if ((white ? state->chess.rights.white_can_castle_queenside : state->chess.rights.black_can_castle_queenside) && queenside_rook_ok) {
        if (game_piece_at(state, 1, back_rank) == '.' && game_piece_at(state, 2, back_rank) == '.' && game_piece_at(state, 3, back_rank) == '.') {
            if (!game_square_attacked_by(state, 2, back_rank, !white) && !game_square_attacked_by(state, 3, back_rank, !white)) {
                MoveHint hint = {.flags = MOVE_CASTLE_QUEEN};
                add_move_if_legal(state, file, rank, 2, back_rank, hint);
            }
        }
    }
}

void game_eval_legal_moves_chess(GameState *state, int file, int rank) {
    if (!state) return;
    game_clear_move_flags(state);
    if (!in_bounds(file, rank)) {
        return;
    }
    char piece = game_piece_at(state, file, rank);
    if (piece == '.') {
        return;
    }
    bool white = game_is_white_piece(piece);
    if ((white && !state->match.white_to_move) || (!white && state->match.white_to_move)) {
        return;
    }

    switch (piece) {
        case 'P':
        case 'p':
            gen_pawn_moves(state, file, rank, white);
            break;
        case 'N':
        case 'n':
            gen_knight_moves(state, file, rank, white);
            break;
        case 'B':
        case 'b': {
            static const int dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
            gen_sliding(state, file, rank, white, dirs, 4);
            break;
        }
        case 'R':
        case 'r': {
            static const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            gen_sliding(state, file, rank, white, dirs, 4);
            break;
        }
        case 'Q':
        case 'q': {
            static const int dirs[8][2] = {{1,1},{1,-1},{-1,1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}};
            gen_sliding(state, file, rank, white, dirs, 8);
            break;
        }
        case 'K':
        case 'k':
            gen_king_moves(state, file, rank, white);
            break;
        default:
            break;
    }
}

void apply_chess_move_common(GameState *state, BoardPos from, BoardPos to, MoveHint hint) {
    snapshot_push(state);
    state->ui.undo_used = false;
    state->last_move.start = from;
    state->last_move.end = to;
    char moving = game_piece_at(state, from.file, from.rank);
    char captured = '.';
    if (hint.flags & MOVE_EN_PASSANT) {
        int cap_rank = to.rank + (moving == 'P' ? 1 : -1);
        if (in_bounds(to.file, cap_rank)) {
            captured = game_piece_at(state, to.file, cap_rank);
        }
    } else {
        captured = game_piece_at(state, to.file, to.rank);
    }
    char promo = ((hint.flags & MOVE_PROMOTION) && hint.promotion) ? hint.promotion : '\0';
    record_capture(state, moving, captured);
    record_move(state, from, to, moving, promo, state->match.white_to_move, hint, captured);
    apply_basic_move(state, from.file, from.rank, to.file, to.rank, hint, true);
    update_game_phase_chess(state);
    game_clear_move_flags(state);
    state->moves.selected.file = -1;
    state->moves.selected.rank = -1;
    state->match.turn_number += 1;
}
