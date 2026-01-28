#include "ai/ai.h"

#include <string.h>

#include "save/replay.h"
#define AI_MAX_MOVES 256
#define AI_INF 1000000
#define AI_MATE 100000

typedef struct {
    BoardPos from;
    BoardPos to;
    MoveHint hint;
} AiMove;

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
            if (game_is_white_piece(piece)) score += value;
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
            if (game_is_white_piece(piece)) score += value;
            else score -= value;
        }
    }
    return score;
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
            if (white_to_move ? !game_is_white_piece(piece) : !game_is_black_piece(piece)) continue;
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
    bool in_check = game_is_in_check(state, state->match.white_to_move);
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
    if (game_is_terminal_phase(state->match.phase)) return;

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

    if (game_apply_move(state, moves[best_index].from, moves[best_index].to, moves[best_index].hint)) {
        if (replay_global_recording()) {
            char promo = '\0';
            if ((moves[best_index].hint.flags & MOVE_PROMOTION) && moves[best_index].hint.promotion) {
                promo = moves[best_index].hint.promotion;
            }
            replay_recorder_record_move(replay_global_recorder(), SDL_GetTicks(),
                moves[best_index].from.file, moves[best_index].from.rank,
                moves[best_index].to.file, moves[best_index].to.rank,
                promo);
        }
    }
}
