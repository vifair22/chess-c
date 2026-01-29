#include "game/logic_internal.h"

#include <string.h>
#include <stdlib.h>

void game_copy_state_minimal(GameState *dst, const GameState *src) {
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

void snapshot_store(GameSnapshot *dst, const GameState *state) {
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

void snapshot_push(GameState *state) {
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
    GameSnapshot *snap = &state->snapshots[--state->snapshot_len];
    state->board_state = snap->board_state;
    state->match = snap->match;
    state->last_move = snap->last_move;
    state->variant = snap->variant;
    state->chess = snap->chess;
    state->white_captures_len = snap->white_captures_len;
    memcpy(state->white_captures, snap->white_captures, sizeof(state->white_captures));
    state->black_captures_len = snap->black_captures_len;
    memcpy(state->black_captures, snap->black_captures, sizeof(state->black_captures));
    state->move_history_len = snap->move_history_len;
    memcpy(state->move_history, snap->move_history, sizeof(state->move_history));
    if (update_anim) {
        BoardPos from = {-1, -1};
        BoardPos to = {-1, -1};
        char from_piece = '.';
        for (int r = 0; r < 8; ++r) {
            for (int f = 0; f < 8; ++f) {
                char cur = current_board.squares[r][f].piece;
                char prev = snap->board_state.squares[r][f].piece;
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
                    char prev = snap->board_state.squares[r][f].piece;
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
uint64_t position_hash(const GameState *state) {
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

void record_position(GameState *state) {
    if (!state) return;
    uint64_t h = position_hash(state);
    if (state->chess.history.len >= (int)(sizeof(state->chess.history.hashes) / sizeof(state->chess.history.hashes[0]))) {
        memmove(state->chess.history.hashes, state->chess.history.hashes + 1, (state->chess.history.len - 1) * sizeof(uint64_t));
        state->chess.history.len -= 1;
    }
    state->chess.history.hashes[state->chess.history.len++] = h;
}

void reset_position_history(GameState *state) {
    if (!state) return;
    state->chess.history.len = 0;
    record_position(state);
}

void game_reset_history(GameState *state) {
    reset_position_history(state);
}

int repetition_count(const GameState *state) {
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

bool is_insufficient_material(const GameState *state) {
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
