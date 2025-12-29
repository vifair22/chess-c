#include "game/logic.h"

#include <string.h>

static bool is_white_piece(char piece) {
    return piece >= 'A' && piece <= 'Z';
}

static bool is_black_piece(char piece) {
    return piece >= 'a' && piece <= 'z';
}

static bool game_mode_is(const GameState *state, const char *mode) {
    return strncmp(state->game_mode, mode, sizeof(state->game_mode)) == 0;
}

static bool is_terminal_phase(const char *phase) {
    return strncmp(phase, "checkmate", 9) == 0 ||
           strncmp(phase, "stalemate", 9) == 0 ||
           strncmp(phase, "win_", 4) == 0 ||
           strncmp(phase, "draw", 4) == 0;
}

static bool game_allows_moves(const GameState *state) {
    // Block further input after terminal results.
    return !is_terminal_phase(state->game_phase);
}

static bool in_bounds(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

static inline SquareState *board_square_ptr(BoardState *board, int file, int rank) {
    return &board->squares[rank][file];
}

static bool is_empty(const GameState *state, int file, int rank);
static bool is_opponent_piece(const GameState *state, int file, int rank, bool white);

static void copy_state_minimal(GameState *dst, const GameState *src) {
    *dst = *src;
    // mutex pointer should not be copied; this is used only for simulation.
    dst->mutex = NULL;
}

static void init_board_chess(GameState *state) {
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
            state->legal_moves[rank][file].flags = 0;
            state->legal_moves[rank][file].promotion = '\0';
        }
    }
}

static void init_board_checkers(GameState *state) {
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
            state->legal_moves[rank][file].flags = 0;
            state->legal_moves[rank][file].promotion = '\0';
        }
    }
}

static void init_board_for_mode(GameState *state) {
    if (game_mode_is(state, "checkers")) {
        init_board_checkers(state);
    } else {
        init_board_chess(state);
    }
}

void game_resign(GameState *state) {
    if (!state || is_terminal_phase(state->game_phase)) {
        return;
    }
    const char *phase = state->white_to_move ? "win_black_resign" : "win_white_resign";
    strncpy(state->game_phase, phase, sizeof(state->game_phase) - 1);
    state->game_phase[sizeof(state->game_phase) - 1] = '\0';
    state->show_pause_menu = false;
    state->show_main_menu = false;
    state->selected_file = -1;
    state->selected_rank = -1;
}

// === Chess logic ===

static void apply_basic_move(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint) {
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
        state->white_can_castle_kingside = false;
        state->white_can_castle_queenside = false;
    } else if (moving == 'k') {
        state->black_can_castle_kingside = false;
        state->black_can_castle_queenside = false;
    } else if (moving == 'R') {
        if (from_rank == 7 && from_file == 0) state->white_can_castle_queenside = false;
        if (from_rank == 7 && from_file == 7) state->white_can_castle_kingside = false;
    } else if (moving == 'r') {
        if (from_rank == 0 && from_file == 0) state->black_can_castle_queenside = false;
        if (from_rank == 0 && from_file == 7) state->black_can_castle_kingside = false;
    }
    // Capture rook on its starting square removes opponent's right.
    if (captured == 'R') {
        if (to_rank == 7 && to_file == 0) state->white_can_castle_queenside = false;
        if (to_rank == 7 && to_file == 7) state->white_can_castle_kingside = false;
    } else if (captured == 'r') {
        if (to_rank == 0 && to_file == 0) state->black_can_castle_queenside = false;
        if (to_rank == 0 && to_file == 7) state->black_can_castle_kingside = false;
    }

    // En passant target update
    state->en_passant_target = (BoardPos){-1, -1};
    if (moving == 'P' && from_rank == 6 && to_rank == 4) {
        state->en_passant_target = (BoardPos){from_file, 5};
    } else if (moving == 'p' && from_rank == 1 && to_rank == 3) {
        state->en_passant_target = (BoardPos){from_file, 2};
    }

    state->white_to_move = !state->white_to_move;
}
void game_clear_move_flags(GameState *state) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            SquareState *sq = board_square_ptr(&state->board_state, file, rank);
            sq->flags &= ~(SQUARE_LEGAL | SQUARE_HIGHLIGHT);
            state->legal_moves[rank][file].flags = 0;
            state->legal_moves[rank][file].promotion = '\0';
        }
    }
}

char game_piece_at(const GameState *state, int file, int rank) {
    return state->board_state.squares[rank][file].piece;
}

static BoardPos find_king(const GameState *state, bool white) {
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

static bool square_attacked_by(const GameState *state, int file, int rank, bool by_white) {
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
static bool is_in_check(const GameState *state, bool white) {
    BoardPos king = find_king(state, white);
    if (king.file == -1 || king.rank == -1) {
        return false;
    }
    return square_attacked_by(state, king.file, king.rank, !white);
}

static bool mark_check_path(GameState *state, int kf, int kr, bool by_white) {
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

static bool has_any_legal_move(const GameState *state, bool white_to_move) {
    GameState tmp;
    copy_state_minimal(&tmp, state);
    tmp.white_to_move = white_to_move;

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = game_piece_at(&tmp, file, rank);
            if (piece == '.') continue;
            if (white_to_move ? !is_white_piece(piece) : !is_black_piece(piece)) {
                continue;
            }
            game_eval_legal_moves(&tmp, file, rank);
            for (int tr = 0; tr < 8; ++tr) {
                for (int tf = 0; tf < 8; ++tf) {
                    if (tmp.legal_moves[tr][tf].flags & MOVE_LEGAL) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static void update_game_phase_chess(GameState *state) {
    bool to_move_white = state->white_to_move;
    bool check = is_in_check(state, to_move_white);
    bool can_move = has_any_legal_move(state, to_move_white);

    // Clear existing check markers.
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            state->board_state.squares[r][f].flags &= ~SQUARE_CHECK;
        }
    }

    if (can_move) {
        const char *phase = check ? "check" : "play";
        strncpy(state->game_phase, phase, sizeof(state->game_phase) - 1);
        state->game_phase[sizeof(state->game_phase) - 1] = '\0';
    } else {
        const char *phase = check ? "checkmate" : "stalemate";
        strncpy(state->game_phase, phase, sizeof(state->game_phase) - 1);
        state->game_phase[sizeof(state->game_phase) - 1] = '\0';
    }

    if (check) {
        BoardPos king = find_king(state, to_move_white);
        if (king.file >= 0 && king.rank >= 0) {
            mark_check_path(state, king.file, king.rank, !to_move_white);
        }
    }
}

// === Checkers logic ===

static bool checkers_is_king(char piece) {
    return piece == 'K' || piece == 'k';
}

static bool checkers_can_capture_from(const GameState *state, int file, int rank, char piece) {
    bool white = is_white_piece(piece);
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
        if (white ? is_white_piece(mid_piece) : is_black_piece(mid_piece)) continue;
        if (game_piece_at(state, landing_file, landing_rank) == '.') {
            return true;
        }
    }
    return false;
}

static bool checkers_player_has_capture(const GameState *state, bool white) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char p = game_piece_at(state, file, rank);
            if (p == '.') continue;
            if (white ? !is_white_piece(p) : !is_black_piece(p)) continue;
            if (checkers_can_capture_from(state, file, rank, p)) {
                return true;
            }
        }
    }
    return false;
}

static bool checkers_piece_has_simple_step(const GameState *state, int file, int rank, char piece) {
    bool white = is_white_piece(piece);
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

static bool checkers_player_has_any_move(const GameState *state, bool white) {
    bool has_capture = checkers_player_has_capture(state, white);
    if (has_capture) {
        return true; // capture exists, so a move exists
    }
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char p = game_piece_at(state, file, rank);
            if (p == '.') continue;
            if (white ? !is_white_piece(p) : !is_black_piece(p)) continue;
            if (checkers_piece_has_simple_step(state, file, rank, p)) {
                return true;
            }
        }
    }
    return false;
}

static void update_game_phase_checkers(GameState *state) {
    int white_pieces = 0;
    int black_pieces = 0;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char p = game_piece_at(state, file, rank);
            if (p == '.') continue;
            if (is_white_piece(p)) ++white_pieces;
            else ++black_pieces;
        }
    }

    if (white_pieces == 0) {
        strncpy(state->game_phase, "win_black", sizeof(state->game_phase) - 1);
        state->game_phase[sizeof(state->game_phase) - 1] = '\0';
        return;
    }
    if (black_pieces == 0) {
        strncpy(state->game_phase, "win_white", sizeof(state->game_phase) - 1);
        state->game_phase[sizeof(state->game_phase) - 1] = '\0';
        return;
    }

    bool to_move_white = state->white_to_move;
    bool can_move = checkers_player_has_any_move(state, to_move_white);
    if (!can_move) {
        const char *phase = to_move_white ? "win_black" : "win_white";
        strncpy(state->game_phase, phase, sizeof(state->game_phase) - 1);
        state->game_phase[sizeof(state->game_phase) - 1] = '\0';
    } else {
        strncpy(state->game_phase, "play", sizeof(state->game_phase) - 1);
        state->game_phase[sizeof(state->game_phase) - 1] = '\0';
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
    bool white = is_white_piece(moving);
    int promo_rank = white ? 0 : 7;
    if (!checkers_is_king(moving) && to_rank == promo_rank) {
        hint.flags |= MOVE_PROMOTION;
        hint.promotion = white ? 'K' : 'k';
    }

    SquareState *dst = board_square_ptr(&state->board_state, to_file, to_rank);
    dst->flags |= SQUARE_LEGAL;
    state->legal_moves[to_rank][to_file] = hint;
    state->legal_moves[to_rank][to_file].flags |= MOVE_LEGAL;
}

static void checkers_gen_moves_for_piece(GameState *state, int file, int rank, bool force_capture) {
    char piece = game_piece_at(state, file, rank);
    if (piece == '.') return;
    bool white = is_white_piece(piece);
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

static void game_eval_legal_moves_checkers(GameState *state, int file, int rank) {
    if (!state) return;
    game_clear_move_flags(state);
    if (!in_bounds(file, rank)) return;
    char piece = game_piece_at(state, file, rank);
    if (piece == '.') return;
    bool white = is_white_piece(piece);
    if ((white && !state->white_to_move) || (!white && state->white_to_move)) {
        return;
    }
    if (state->checkers_must_continue &&
        (file != state->selected_file || rank != state->selected_rank)) {
        return; // must continue jumping with the same piece
    }

    bool force_capture = checkers_player_has_capture(state, state->white_to_move);
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

    state->move_start = (BoardPos){from_file, from_rank};
    state->move_end = (BoardPos){to_file, to_rank};
    state->en_passant_target = (BoardPos){-1, -1};

    bool more_captures = capture && checkers_can_capture_from(state, to_file, to_rank, placed);
    if (more_captures) {
        state->selected_file = to_file;
        state->selected_rank = to_rank;
        state->checkers_must_continue = true;
        game_clear_move_flags(state);
        game_eval_legal_moves_checkers(state, to_file, to_rank);
    } else {
        state->checkers_must_continue = false;
        state->white_to_move = !state->white_to_move;
        game_clear_move_flags(state);
        state->selected_file = -1;
        state->selected_rank = -1;
        state->turn_number += 1;
        update_game_phase_checkers(state);
    }
}
bool game_init(GameState *state) {
    state->mutex = SDL_CreateMutex();
    if (!state->mutex) {
        return false;
    }

    game_reset(state, state->game_mode[0] ? state->game_mode : "chess");
    state->show_main_menu = true;
    state->show_pause_menu = false;

    return true;
}

void game_destroy(GameState *state) {
    if (state && state->mutex) {
        SDL_DestroyMutex(state->mutex);
        state->mutex = NULL;
    }
}

static void game_handle_click_chess(GameState *state, int file, int rank) {
    char clicked = game_piece_at(state, file, rank);
    bool clicked_white = is_white_piece(clicked);
    bool clicked_black = is_black_piece(clicked);

    bool has_selection = state->selected_file >= 0 && state->selected_rank >= 0;

    if (!has_selection) {
        if (clicked != '.' &&
            ((state->white_to_move && clicked_white) ||
             (!state->white_to_move && clicked_black))) {
            state->selected_file = file;
            state->selected_rank = rank;
            game_eval_legal_moves(state, file, rank);
        }
        return;
    }

    // Clicking the same square toggles selection off.
    if (state->selected_file == file && state->selected_rank == rank) {
        state->selected_file = -1;
        state->selected_rank = -1;
        game_clear_move_flags(state);
        return;
    }

    char selected_piece = game_piece_at(state, state->selected_file, state->selected_rank);
    bool selected_white = is_white_piece(selected_piece);

    // Switch selection to another friendly piece.
    if (clicked != '.' && ((selected_white && clicked_white) || (!selected_white && clicked_black))) {
        state->selected_file = file;
        state->selected_rank = rank;
        game_eval_legal_moves(state, file, rank);
        return;
    }

    MoveHint hint = state->legal_moves[rank][file];
    if (!(hint.flags & MOVE_LEGAL)) {
        return;
    }

    // Apply legal move with metadata (handles castles/en passant/promo).
    state->move_start = (BoardPos){state->selected_file, state->selected_rank};
    state->move_end = (BoardPos){file, rank};
    apply_basic_move(state, state->selected_file, state->selected_rank, file, rank, hint);

    update_game_phase_chess(state);

    game_clear_move_flags(state);
    state->selected_file = -1;
    state->selected_rank = -1;
    state->turn_number += 1;
}

static void game_handle_click_checkers(GameState *state, int file, int rank) {
    char clicked = game_piece_at(state, file, rank);
    bool clicked_white = is_white_piece(clicked);
    bool clicked_black = is_black_piece(clicked);
    bool has_selection = state->selected_file >= 0 && state->selected_rank >= 0;

    if (!has_selection) {
        if (clicked != '.' &&
            ((state->white_to_move && clicked_white) ||
             (!state->white_to_move && clicked_black))) {
            state->selected_file = file;
            state->selected_rank = rank;
            game_eval_legal_moves_checkers(state, file, rank);
        }
        return;
    }

    // In a multi-jump chain, the player must continue with the same piece.
    if (state->checkers_must_continue) {
        if (state->selected_file == file && state->selected_rank == rank) {
            return;
        }
    } else {
        // Clicking the same square toggles selection off.
        if (state->selected_file == file && state->selected_rank == rank) {
            state->selected_file = -1;
            state->selected_rank = -1;
            game_clear_move_flags(state);
            return;
        }

        // Switch selection to another friendly piece.
        if (clicked != '.' &&
            ((state->white_to_move && clicked_white) ||
             (!state->white_to_move && clicked_black))) {
            state->selected_file = file;
            state->selected_rank = rank;
            game_eval_legal_moves_checkers(state, file, rank);
            return;
        }
    }

    MoveHint hint = state->legal_moves[rank][file];
    if (!(hint.flags & MOVE_LEGAL)) {
        return;
    }

    apply_checkers_move(state, state->selected_file, state->selected_rank, file, rank, hint);
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

static void add_move_if_legal(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint) {
    if (!in_bounds(to_file, to_rank)) {
        return;
    }
    bool moving_white = is_white_piece(game_piece_at(state, from_file, from_rank));
    char target_piece = game_piece_at(state, to_file, to_rank);
    if (target_piece == (moving_white ? 'k' : 'K')) {
        return; // cannot capture the enemy king
    }
    GameState tmp;
    copy_state_minimal(&tmp, state);
    apply_basic_move(&tmp, from_file, from_rank, to_file, to_rank, hint);
    BoardPos king = find_king(&tmp, moving_white);
    if (king.file == -1 || square_attacked_by(&tmp, king.file, king.rank, !moving_white)) {
        return; // leaves or puts own king in check
    }

    SquareState *dst = board_square_ptr(&state->board_state, to_file, to_rank);
    dst->flags |= SQUARE_LEGAL;
    state->legal_moves[to_rank][to_file] = hint;
    state->legal_moves[to_rank][to_file].flags |= MOVE_LEGAL;
}

static bool is_empty(const GameState *state, int file, int rank) {
    return in_bounds(file, rank) && game_piece_at(state, file, rank) == '.';
}

static bool is_opponent_piece(const GameState *state, int file, int rank, bool white) {
    char p = game_piece_at(state, file, rank);
    if (p == '.') return false;
    return white ? is_black_piece(p) : is_white_piece(p);
}

static void gen_pawn_moves(GameState *state, int file, int rank, bool white) {
    int dir = white ? -1 : 1;
    int start_rank = white ? 6 : 1;
    int promo_rank = white ? 0 : 7;
    char promo_piece = white ? state->promotion_choice_white : state->promotion_choice_black;
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
    if (state->en_passant_target.file != -1) {
        if (state->en_passant_target.rank == rank + dir &&
            (state->en_passant_target.file == file - 1 || state->en_passant_target.file == file + 1)) {
            MoveHint hint = {.flags = MOVE_EN_PASSANT};
            add_move_if_legal(state, file, rank, state->en_passant_target.file, state->en_passant_target.rank, hint);
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
        if (target != '.' && ((white && is_white_piece(target)) || (!white && is_black_piece(target)))) continue;
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
                if ((white && is_white_piece(target)) || (!white && is_black_piece(target))) {
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
            if (target != '.' && ((white && is_white_piece(target)) || (!white && is_black_piece(target)))) continue;
            MoveHint hint = {0};
            add_move_if_legal(state, file, rank, tf, tr, hint);
        }
    }

    // Castling
    bool in_check = square_attacked_by(state, file, rank, !white);
    if (in_check) return;
    int back_rank = white ? 7 : 0;
    char rook_k = game_piece_at(state, 7, back_rank);
    char rook_q = game_piece_at(state, 0, back_rank);
    bool kingside_rook_ok = white ? (rook_k == 'R') : (rook_k == 'r');
    bool queenside_rook_ok = white ? (rook_q == 'R') : (rook_q == 'r');
    if ((white ? state->white_can_castle_kingside : state->black_can_castle_kingside) && kingside_rook_ok) {
        if (game_piece_at(state, 5, back_rank) == '.' && game_piece_at(state, 6, back_rank) == '.') {
            if (!square_attacked_by(state, 5, back_rank, !white) && !square_attacked_by(state, 6, back_rank, !white)) {
                MoveHint hint = {.flags = MOVE_CASTLE_KING};
                add_move_if_legal(state, file, rank, 6, back_rank, hint);
            }
        }
    }
    if ((white ? state->white_can_castle_queenside : state->black_can_castle_queenside) && queenside_rook_ok) {
        if (game_piece_at(state, 1, back_rank) == '.' && game_piece_at(state, 2, back_rank) == '.' && game_piece_at(state, 3, back_rank) == '.') {
            if (!square_attacked_by(state, 2, back_rank, !white) && !square_attacked_by(state, 3, back_rank, !white)) {
                MoveHint hint = {.flags = MOVE_CASTLE_QUEEN};
                add_move_if_legal(state, file, rank, 2, back_rank, hint);
            }
        }
    }
}

static void game_eval_legal_moves_chess(GameState *state, int file, int rank) {
    if (!state) return;
    game_clear_move_flags(state);
    if (!in_bounds(file, rank)) {
        return;
    }
    char piece = game_piece_at(state, file, rank);
    if (piece == '.') {
        return;
    }
    bool white = is_white_piece(piece);
    if ((white && !state->white_to_move) || (!white && state->white_to_move)) {
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

void game_eval_legal_moves(GameState *state, int file, int rank) {
    if (game_mode_is(state, "checkers")) {
        game_eval_legal_moves_checkers(state, file, rank);
    } else {
        game_eval_legal_moves_chess(state, file, rank);
    }
}

void game_reset(GameState *state, const char *mode) {
    if (!mode || mode[0] == '\0') {
        mode = "chess";
    }
    strncpy(state->game_mode, mode, sizeof(state->game_mode) - 1);
    state->game_mode[sizeof(state->game_mode) - 1] = '\0';

    init_board_for_mode(state);

    state->white_to_move = game_mode_is(state, "checkers") ? false : true;
    state->selected_file = -1;
    state->selected_rank = -1;
    state->hover_pos = (BoardPos){-1, -1};

    state->turn_number = 0;
    state->game_time.total_seconds = 0.0;
    strncpy(state->game_phase, "play", sizeof(state->game_phase) - 1);
    state->game_phase[sizeof(state->game_phase) - 1] = '\0';
    state->move_start = (BoardPos){-1, -1};
    state->move_end = (BoardPos){-1, -1};
    state->white_can_castle_kingside = true;
    state->white_can_castle_queenside = true;
    state->black_can_castle_kingside = true;
    state->black_can_castle_queenside = true;
    state->en_passant_target = (BoardPos){-1, -1};
    state->checkers_must_continue = false;
    state->smoothed_frame_ms = 0.0;
    state->smoothed_fps = 0.0;
    state->perf_accum_time = 0.0;
    state->perf_accum_frames = 0;
    state->show_debug = true;
    state->promotion_choice_white = 'Q';
    state->promotion_choice_black = 'q';

    game_clear_move_flags(state);
}
