#include "game/logic.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define GAME_SNAPSHOT_MAX 256

typedef struct GameSnapshot {
    BoardState board_state;
    MatchState match;
    LastMove last_move;
    VariantState variant;
    ChessRulesState chess;
    char white_captures[32];
    int white_captures_len;
    char black_captures[32];
    int black_captures_len;
    MoveRecord move_history[512];
    int move_history_len;
} GameSnapshot;

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

static bool game_mode_is(const GameState *state, const char *mode) {
    return strncmp(state->match.mode, mode, sizeof(state->match.mode)) == 0;
}

static void copy_state_minimal(GameState *dst, const GameState *src);
static void apply_basic_move(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint, bool update_counters);
static void update_game_phase_chess(GameState *state);

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

static void record_capture(GameState *state, char capturer, char captured) {
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
    GameState tmp;
    copy_state_minimal(&tmp, state);
    game_eval_legal_moves(&tmp, from.file, from.rank);
    MoveHint hint = tmp.moves.legal_moves[to.rank][to.file];
    return (hint.flags & MOVE_LEGAL) != 0;
}

static void build_san(const GameState *state, BoardPos from, BoardPos to, MoveHint hint,
    char moving, char captured, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    bool is_checkers = game_mode_is(state, "checkers");
    if (is_checkers) {
        snprintf(out, cap, "%c%d-%c%d",
            (char)('a' + from.file), from.rank + 1,
            (char)('a' + to.file), to.rank + 1);
        return;
    }
    if (hint.flags & MOVE_CASTLE_KING) {
        snprintf(out, cap, "O-O");
    } else if (hint.flags & MOVE_CASTLE_QUEEN) {
        snprintf(out, cap, "O-O-O");
    } else {
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
                    disamb[1] = (char)('1' + from.rank);
                    disamb[2] = '\0';
                } else if (same_file) {
                    disamb[0] = (char)('1' + from.rank);
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
        dest[1] = (char)('1' + to.rank);
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
    GameState tmp;
    copy_state_minimal(&tmp, state);
    apply_basic_move(&tmp, from.file, from.rank, to.file, to.rank, hint, false);
    update_game_phase_chess(&tmp);
    if (strncmp(tmp.match.phase, "checkmate", 9) == 0) {
        strncat(out, "#", cap - strlen(out) - 1);
    } else if (game_is_in_check(&tmp, tmp.match.white_to_move)) {
        strncat(out, "+", cap - strlen(out) - 1);
    }
}

static void record_move(GameState *state, BoardPos from, BoardPos to, char piece, char promotion, bool white_move, MoveHint hint, char captured) {
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

static bool game_allows_moves(const GameState *state) {
    return game_allows_moves_ex(state, false);
}

static bool in_bounds(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

static inline SquareState *board_square_ptr(BoardState *board, int file, int rank) {
    return &board->squares[rank][file];
}

static bool is_empty(const GameState *state, int file, int rank);
static bool is_opponent_piece(const GameState *state, int file, int rank, bool white);
static bool game_apply_move_internal(GameState *state, BoardPos from, BoardPos to, MoveHint hint, bool allow_ai_turn, const char *context);

static void copy_state_minimal(GameState *dst, const GameState *src) {
    if (!dst || !src) return;
    memset(dst, 0, sizeof(*dst));
    dst->board_state = src->board_state;
    dst->match = src->match;
    dst->variant = src->variant;
    dst->chess = src->chess;
    dst->moves.selected = src->moves.selected;
    dst->moves.hover_pos = (BoardPos){-1, -1};
    dst->move_history_len = 0;
    memset(dst->move_history, 0, sizeof(dst->move_history));
    dst->mutex = NULL;
    dst->snapshots = NULL;
    dst->snapshot_len = 0;
    dst->snapshot_cap = 0;
}

static void snapshot_store(GameSnapshot *dst, const GameState *state) {
    dst->board_state = state->board_state;
    dst->match = state->match;
    dst->last_move = state->last_move;
    dst->variant = state->variant;
    dst->chess = state->chess;
    dst->white_captures_len = state->white_captures_len;
    memcpy(dst->white_captures, state->white_captures, sizeof(dst->white_captures));
    dst->black_captures_len = state->black_captures_len;
    memcpy(dst->black_captures, state->black_captures, sizeof(dst->black_captures));
    dst->move_history_len = state->move_history_len;
    memcpy(dst->move_history, state->move_history, sizeof(dst->move_history));
}

static void snapshot_push(GameState *state) {
    if (!state || !state->snapshots || state->snapshot_cap <= 0) return;
    if (state->snapshot_len >= state->snapshot_cap) {
        memmove(state->snapshots, state->snapshots + 1, (size_t)(state->snapshot_cap - 1) * sizeof(GameSnapshot));
        state->snapshot_len = state->snapshot_cap - 1;
    }
    snapshot_store(&state->snapshots[state->snapshot_len++], state);
}

bool game_undo_ex(GameState *state, bool update_anim) {
    if (!state || !state->snapshots || state->snapshot_len <= 0) {
        return false;
    }
    BoardState current_board = state->board_state;
    LastMove anim_keep = state->last_move;
    GameSnapshot snap = state->snapshots[--state->snapshot_len];
    state->board_state = snap.board_state;
    state->match = snap.match;
    state->last_move = snap.last_move;
    state->variant = snap.variant;
    state->chess = snap.chess;
    state->white_captures_len = snap.white_captures_len;
    memcpy(state->white_captures, snap.white_captures, sizeof(state->white_captures));
    state->black_captures_len = snap.black_captures_len;
    memcpy(state->black_captures, snap.black_captures, sizeof(state->black_captures));
    state->move_history_len = snap.move_history_len;
    memcpy(state->move_history, snap.move_history, sizeof(state->move_history));
    if (update_anim) {
        BoardPos from = {-1, -1};
        BoardPos to = {-1, -1};
        char from_piece = '.';
        for (int r = 0; r < 8; ++r) {
            for (int f = 0; f < 8; ++f) {
                char cur = current_board.squares[r][f].piece;
                char prev = snap.board_state.squares[r][f].piece;
                if (cur != '.' && prev == '.') {
                    from = (BoardPos){f, r};
                    from_piece = cur;
                    break;
                }
            }
            if (from.file >= 0) break;
        }
        if (from.file >= 0) {
            bool from_white = game_is_white_piece(from_piece);
            for (int r = 0; r < 8; ++r) {
                for (int f = 0; f < 8; ++f) {
                    char cur = current_board.squares[r][f].piece;
                    char prev = snap.board_state.squares[r][f].piece;
                    if (cur == '.' && prev != '.') {
                        if ((from_white && game_is_white_piece(prev)) || (!from_white && game_is_black_piece(prev))) {
                            to = (BoardPos){f, r};
                            break;
                        }
                    }
                }
                if (to.file >= 0) break;
            }
        }
        if (from.file >= 0 && to.file >= 0) {
            state->last_move.start = from;
            state->last_move.end = to;
        }
    } else {
        state->last_move = anim_keep;
    }
    state->moves.selected.file = -1;
    state->moves.selected.rank = -1;
    state->moves.hover_pos = (BoardPos){-1, -1};
    game_clear_move_flags(state);
    return true;
}

bool game_undo(GameState *state) {
    return game_undo_ex(state, true);
}

bool game_peek_undo_white_to_move(const GameState *state, bool *out_white_to_move) {
    if (!state || !out_white_to_move || !state->snapshots || state->snapshot_len <= 0) {
        return false;
    }
    *out_white_to_move = state->snapshots[state->snapshot_len - 1].match.white_to_move;
    return true;
}

// Simple FNV-1a style hash of the full position (pieces, rights, side to move, en-passant)
static uint64_t position_hash(const GameState *state) {
    const uint64_t fnv_offset = 0xcbf29ce484222325ULL;
    const uint64_t fnv_prime = 0x100000001b3ULL;
    uint64_t h = fnv_offset;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char p = game_piece_at(state, file, rank);
            // include coordinates to reduce collisions on empty squares
            uint64_t v = (uint64_t)(uint8_t)p ^ (uint64_t)((rank * 8 + file) + 1);
            h ^= v;
            h *= fnv_prime;
        }
    }
    uint64_t rights = 0;
    if (state->chess.rights.white_can_castle_kingside) rights |= 1ULL;
    if (state->chess.rights.white_can_castle_queenside) rights |= 2ULL;
    if (state->chess.rights.black_can_castle_kingside) rights |= 4ULL;
    if (state->chess.rights.black_can_castle_queenside) rights |= 8ULL;
    h ^= rights + 0x9e3779b97f4a7c15ULL;
    h *= fnv_prime;
    uint64_t ep_file = (uint64_t)(state->chess.rights.en_passant_target.file + 2); // -1 becomes 1
    uint64_t ep_rank = (uint64_t)(state->chess.rights.en_passant_target.rank + 2);
    h ^= (ep_file << 8) ^ (ep_rank << 12);
    h *= fnv_prime;
    h ^= state->match.white_to_move ? 0xabc98388fb8fac03ULL : 0;
    h *= fnv_prime;
    return h;
}

static void record_position(GameState *state) {
    if (!state) return;
    uint64_t h = position_hash(state);
    if (state->chess.history.len >= (int)(sizeof(state->chess.history.hashes) / sizeof(state->chess.history.hashes[0]))) {
        memmove(state->chess.history.hashes, state->chess.history.hashes + 1, (state->chess.history.len - 1) * sizeof(uint64_t));
        state->chess.history.len -= 1;
    }
    state->chess.history.hashes[state->chess.history.len++] = h;
}

static void reset_position_history(GameState *state) {
    if (!state) return;
    state->chess.history.len = 0;
    record_position(state);
}

void game_reset_history(GameState *state) {
    reset_position_history(state);
}

static int repetition_count(const GameState *state) {
    if (!state || state->chess.history.len <= 0) return 0;
    uint64_t cur = state->chess.history.hashes[state->chess.history.len - 1];
    int count = 0;
    for (int i = 0; i < state->chess.history.len; ++i) {
        if (state->chess.history.hashes[i] == cur) {
            count += 1;
        }
    }
    return count;
}

static bool is_insufficient_material(const GameState *state) {
    // Detect simple insufficient material cases:
    // K vs K; K vs K + (B or N); K+B vs K+B with bishops on same color.
    int white_bishops = 0, black_bishops = 0;
    int white_knights = 0, black_knights = 0;
    int other_white = 0, other_black = 0;
    int white_bishop_dark = 0, white_bishop_light = 0;
    int black_bishop_dark = 0, black_bishop_light = 0;

    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            char p = game_piece_at(state, f, r);
            if (p == '.' || p == 'K' || p == 'k') continue;
            bool dark = ((f + r) % 2) == 1;
            switch (p) {
                case 'B':
                    white_bishops++;
                    dark ? white_bishop_dark++ : white_bishop_light++;
                    break;
                case 'b':
                    black_bishops++;
                    dark ? black_bishop_dark++ : black_bishop_light++;
                    break;
                case 'N':
                    white_knights++;
                    break;
                case 'n':
                    black_knights++;
                    break;
                default:
                    if (game_is_white_piece(p)) other_white++;
                    else if (game_is_black_piece(p)) other_black++;
                    break;
            }
        }
    }

    if (other_white > 0 || other_black > 0) {
        return false; // pawns/rooks/queens present
    }

    // K vs K
    if ((white_bishops + white_knights + other_white) == 0 &&
        (black_bishops + black_knights + other_black) == 0) {
        return true;
    }

    // K + minor vs K
    if ((white_bishops + white_knights + other_white) == 1 &&
        (black_bishops + black_knights + other_black) == 0) {
        return true;
    }
    if ((black_bishops + black_knights + other_black) == 1 &&
        (white_bishops + white_knights + other_white) == 0) {
        return true;
    }

    // K+B vs K+B where all bishops are on the same color squares
    bool only_bishops =
        (white_knights + black_knights + other_white + other_black) == 0 &&
        white_bishops > 0 && black_bishops > 0;
    if (only_bishops) {
        bool white_all_dark = white_bishop_dark == white_bishops;
        bool white_all_light = white_bishop_light == white_bishops;
        bool black_all_dark = black_bishop_dark == black_bishops;
        bool black_all_light = black_bishop_light == black_bishops;
        if ((white_all_dark && black_all_dark) || (white_all_light && black_all_light)) {
            return true;
        }
    }

    return false;
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
            state->moves.legal_moves[rank][file].flags = 0;
            state->moves.legal_moves[rank][file].promotion = '\0';
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
            state->moves.legal_moves[rank][file].flags = 0;
            state->moves.legal_moves[rank][file].promotion = '\0';
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

// === Chess logic ===

static void apply_basic_move(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint, bool update_counters) {
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
    tmp.match.white_to_move = white_to_move;

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = game_piece_at(&tmp, file, rank);
            if (piece == '.') continue;
            if (white_to_move ? !game_is_white_piece(piece) : !game_is_black_piece(piece)) {
                continue;
            }
            game_eval_legal_moves(&tmp, file, rank);
            for (int tr = 0; tr < 8; ++tr) {
                for (int tf = 0; tf < 8; ++tf) {
                    if (tmp.moves.legal_moves[tr][tf].flags & MOVE_LEGAL) {
                        return true;
                    }
                }
            }
            game_clear_move_flags(&tmp);
        }
    }
    return false;
}

static void update_game_phase_chess(GameState *state) {
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

// === Checkers logic ===

static bool checkers_is_king(char piece) {
    return piece == 'K' || piece == 'k';
}

static bool checkers_can_capture_from(const GameState *state, int file, int rank, char piece) {
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

static bool checkers_player_has_capture(const GameState *state, bool white) {
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

static bool checkers_player_has_any_move(const GameState *state, bool white) {
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

static void update_game_phase_checkers(GameState *state) {
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

static void game_eval_legal_moves_checkers(GameState *state, int file, int rank) {
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

static void apply_chess_move_common(GameState *state, BoardPos from, BoardPos to, MoveHint hint) {
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

static void apply_checkers_move_common(GameState *state, int from_file, int from_rank, int to_file, int to_rank, MoveHint hint, bool snapshot) {
    if (snapshot) {
        snapshot_push(state);
        state->ui.undo_used = false;
    }
    char moving = game_piece_at(state, from_file, from_rank);
    record_move(state, (BoardPos){from_file, from_rank}, (BoardPos){to_file, to_rank}, moving, '\0', state->match.white_to_move, hint, '\0');
    apply_checkers_move(state, from_file, from_rank, to_file, to_rank, hint);
}
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
    state->ai_time_freq = (double)SDL_GetPerformanceFrequency();

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
        }
        return;
    }

    // Clicking the same square toggles selection off.
    if (state->moves.selected.file == file && state->moves.selected.rank == rank) {
        state->moves.selected.file = -1;
        state->moves.selected.rank = -1;
        game_clear_move_flags(state);
        return;
    }

    char selected_piece = game_piece_at(state, state->moves.selected.file, state->moves.selected.rank);
    bool selected_white = game_is_white_piece(selected_piece);

    // Switch selection to another friendly piece.
    if (clicked != '.' && ((selected_white && clicked_white) || (!selected_white && clicked_black))) {
        state->moves.selected.file = file;
        state->moves.selected.rank = rank;
        game_eval_legal_moves(state, file, rank);
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
            return;
        }

        // Switch selection to another friendly piece.
        if (clicked != '.' &&
            ((state->match.white_to_move && clicked_white) ||
             (!state->match.white_to_move && clicked_black))) {
            state->moves.selected.file = file;
            state->moves.selected.rank = rank;
            game_eval_legal_moves_checkers(state, file, rank);
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
    return ok;
}

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
    return true;
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
    GameState tmp;
    copy_state_minimal(&tmp, state);
    apply_basic_move(&tmp, from_file, from_rank, to_file, to_rank, hint, false);
    BoardPos king = find_king(&tmp, moving_white);
    if (king.file == -1 || game_square_attacked_by(&tmp, king.file, king.rank, !moving_white)) {
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
    state->ui.main_menu_index = 0;
    state->ui.new_menu_index = 0;
    state->ui.pause_menu_index = 0;
    state->ui.save_menu_index = 0;
    state->ui.load_menu_focus = 0;
    state->ui.load_confirm_index = 0;
    state->ui.menu_keyboard_active = false;
    state->ui.menu_mouse_active = true;
    state->ui.save_len = 0;
    state->ui.save_name[0] = '\0';
    state->ui.load_count = 0;
    state->ui.load_scroll = 0;
    state->ui.load_selected = -1;
    state->ui.load_scroll_dragging = false;
    state->ui.load_scroll_drag_offset = 0;
    state->ui.turn_log_scroll = 0;
    state->ui.turn_log_max_scroll = 0;
    state->ai_last_move_ms = 0.0;
    state->ai_timing_active = false;
    state->ai_timing_start = 0;
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
