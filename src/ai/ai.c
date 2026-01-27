#include "ai/ai.h"

#include <string.h>

#define AI_MAX_MOVES 256
#define AI_INF 1000000
#define AI_MATE 100000

typedef struct {
    BoardPos from;
    BoardPos to;
    MoveHint hint;
} AiMove;

static bool is_white_piece(char piece) {
    return piece >= 'A' && piece <= 'Z';
}

static bool is_black_piece(char piece) {
    return piece >= 'a' && piece <= 'z';
}

static bool is_terminal_phase(const char *phase) {
    if (!phase) return false;
    return strncmp(phase, "checkmate", 9) == 0 ||
           strncmp(phase, "stalemate", 9) == 0 ||
           strncmp(phase, "win_", 4) == 0 ||
           strncmp(phase, "draw", 4) == 0;
}

static int piece_value(char piece) {
    switch (piece) {
        case 'P':
        case 'p':
            return 100;
        case 'N':
        case 'n':
            return 320;
        case 'B':
        case 'b':
            return 330;
        case 'R':
        case 'r':
            return 500;
        case 'Q':
        case 'q':
            return 900;
        default:
            return 0;
    }
}

static int material_score_chess(const GameState *state) {
    int score = 0;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = game_piece_at(state, file, rank);
            if (piece == '.') continue;
            int value = piece_value(piece);
            if (is_white_piece(piece)) score += value;
            else score -= value;
        }
    }
    return score;
}

static int material_score_checkers(const GameState *state) {
    int score = 0;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = game_piece_at(state, file, rank);
            if (piece == '.') continue;
            int value = 0;
            if (piece == 'M' || piece == 'm') value = 100;
            else if (piece == 'K' || piece == 'k') value = 180;
            if (is_white_piece(piece)) score += value;
            else score -= value;
        }
    }
    return score;
}

static bool square_attacked_by(const GameState *state, int file, int rank, bool by_white) {
    int pawn_dir = by_white ? 1 : -1;
    int pawn_rank = rank + pawn_dir;
    if (file - 1 >= 0 && file - 1 < 8 && pawn_rank >= 0 && pawn_rank < 8) {
        char p = game_piece_at(state, file - 1, pawn_rank);
        if ((by_white && p == 'P') || (!by_white && p == 'p')) return true;
    }
    if (file + 1 >= 0 && file + 1 < 8 && pawn_rank >= 0 && pawn_rank < 8) {
        char p = game_piece_at(state, file + 1, pawn_rank);
        if ((by_white && p == 'P') || (!by_white && p == 'p')) return true;
    }

    static const int knight_offsets[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
        {1, -2}, {2, -1}, {-1, -2}, {-2, -1},
    };
    for (int i = 0; i < 8; ++i) {
        int f = file + knight_offsets[i][0];
        int r = rank + knight_offsets[i][1];
        if (f < 0 || f >= 8 || r < 0 || r >= 8) continue;
        char p = game_piece_at(state, f, r);
        if ((by_white && p == 'N') || (!by_white && p == 'n')) return true;
    }

    static const int diag_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = file + diag_dirs[d][0];
        int r = rank + diag_dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
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

    static const int ortho_dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = file + ortho_dirs[d][0];
        int r = rank + ortho_dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
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

    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            int f = file + df;
            int r = rank + dr;
            if (f < 0 || f >= 8 || r < 0 || r >= 8) continue;
            char p = game_piece_at(state, f, r);
            if ((by_white && p == 'K') || (!by_white && p == 'k')) return true;
        }
    }

    return false;
}

static bool is_in_check(const GameState *state, bool white) {
    char king = white ? 'K' : 'k';
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            if (game_piece_at(state, file, rank) == king) {
                return square_attacked_by(state, file, rank, !white);
            }
        }
    }
    return false;
}

static int evaluate(const GameState *state) {
    bool checkers = strncmp(state->match.mode, "checkers", 8) == 0;
    int score = checkers ? material_score_checkers(state) : material_score_chess(state);
    return state->match.white_to_move ? score : -score;
}

static int generate_moves(GameState *state, AiMove *moves, int cap) {
    int count = 0;
    bool white_to_move = state->match.white_to_move;
    bool checkers = strncmp(state->match.mode, "checkers", 8) == 0;

    if (checkers && state->variant.checkers_must_continue) {
        int file = state->moves.selected.file;
        int rank = state->moves.selected.rank;
        if (file < 0 || rank < 0) {
            return 0;
        }
        game_eval_legal_moves(state, file, rank);
        for (int tr = 0; tr < 8; ++tr) {
            for (int tf = 0; tf < 8; ++tf) {
                MoveHint hint = state->moves.legal_moves[tr][tf];
                if (!(hint.flags & MOVE_LEGAL)) continue;
                if (count < cap) {
                    moves[count++] = (AiMove){
                        .from = {file, rank},
                        .to = {tf, tr},
                        .hint = hint,
                    };
                }
            }
        }
        game_clear_move_flags(state);
        return count;
    }

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            char piece = game_piece_at(state, file, rank);
            if (piece == '.') continue;
            if (white_to_move ? !is_white_piece(piece) : !is_black_piece(piece)) continue;
            game_eval_legal_moves(state, file, rank);
            for (int tr = 0; tr < 8; ++tr) {
                for (int tf = 0; tf < 8; ++tf) {
                    MoveHint hint = state->moves.legal_moves[tr][tf];
                    if (!(hint.flags & MOVE_LEGAL)) continue;
                    if (count < cap) {
                        moves[count++] = (AiMove){
                            .from = {file, rank},
                            .to = {tf, tr},
                            .hint = hint,
                        };
                    }
                }
            }
            game_clear_move_flags(state);
        }
    }
    return count;
}

static int terminal_score(GameState *state, int depth) {
    bool checkers = strncmp(state->match.mode, "checkers", 8) == 0;
    if (checkers) {
        return -AI_MATE + depth; // no moves -> loss
    }
    bool in_check = is_in_check(state, state->match.white_to_move);
    if (in_check) {
        return -AI_MATE + depth; // getting mated is bad; depth prefers faster mates
    }
    return 0; // stalemate or draw
}

static int negamax(GameState *state, int depth, int alpha, int beta) {
    AiMove moves[AI_MAX_MOVES];
    int count = generate_moves(state, moves, AI_MAX_MOVES);

    if (count == 0) {
        return terminal_score(state, depth);
    }
    if (depth == 0) {
        return evaluate(state);
    }

    int best = -AI_INF;
    for (int i = 0; i < count; ++i) {
        GameState next = *state;
        next.mutex = NULL;
        if (!game_apply_move(&next, moves[i].from, moves[i].to, moves[i].hint)) {
            continue;
        }
        bool same_turn = next.match.white_to_move == state->match.white_to_move;
        int score = same_turn ? negamax(&next, depth - 1, alpha, beta)
                              : -negamax(&next, depth - 1, -beta, -alpha);
        if (score > best) best = score;
        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }
    return best;
}

void ai_make_move(GameState *state) {
    if (!state) return;
    if (state->ai.side == AI_SIDE_NONE) return;
    if (state->ui.show_main_menu || state->ui.show_pause_menu) return;
    if (is_terminal_phase(state->match.phase)) return;

    bool ai_is_white = state->ai.side == AI_SIDE_WHITE;
    if (state->match.white_to_move != ai_is_white) return;

    int depth = state->ai.depth;
    if (depth < 1) depth = 1;
    if (depth > 4) depth = 4;

    AiMove moves[AI_MAX_MOVES];
    int count = generate_moves(state, moves, AI_MAX_MOVES);
    if (count <= 0) return;

    int best_score = -AI_INF;
    int best_index = 0;
    for (int i = 0; i < count; ++i) {
        GameState next = *state;
        next.mutex = NULL;
        if (!game_apply_move(&next, moves[i].from, moves[i].to, moves[i].hint)) {
            continue;
        }
        bool same_turn = next.match.white_to_move == state->match.white_to_move;
        int score = same_turn ? negamax(&next, depth - 1, -AI_INF, AI_INF)
                              : -negamax(&next, depth - 1, -AI_INF, AI_INF);
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    game_apply_move(state, moves[best_index].from, moves[best_index].to, moves[best_index].hint);
}
