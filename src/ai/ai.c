#include "ai/ai.h"

#include <SDL.h>
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

typedef struct {
    GameState state;
    int depth;
    uint32_t position_key;
    int turn_number;
} AiJob;

typedef struct {
    SDL_Thread *thread;
    SDL_mutex *mutex;
    SDL_cond *cond;
    bool running;
    bool has_job;
    bool busy;
    bool has_result;
    AiJob job;
    AiResult result;
    double last_compute_ms;
} AiWorker;

static AiWorker g_ai_worker;

static void ai_clone_state(GameState *dst, const GameState *src) {
    if (!dst || !src) return;
    *dst = *src;
    dst->mutex = NULL;
    dst->snapshots = NULL;
    dst->snapshot_len = 0;
    dst->snapshot_cap = 0;
}

static void ai_snapshot_state(GameState *dst, const GameState *src) {
    if (!dst || !src) return;
    memset(dst, 0, sizeof(*dst));
    dst->board_state = src->board_state;
    dst->match = src->match;
    dst->variant = src->variant;
    dst->chess = src->chess;
    dst->moves.selected = src->moves.selected;
    dst->moves.hover_pos = (BoardPos){-1, -1};
    dst->mutex = NULL;
    dst->snapshots = NULL;
    dst->snapshot_len = 0;
    dst->snapshot_cap = 0;
    game_clear_move_flags(dst);
}

uint32_t ai_position_key(const GameState *state) {
    if (!state) return 0;
    uint32_t h = 2166136261u;
    #define HSTEP(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            HSTEP(state->board_state.squares[r][f].piece);
        }
    }
    for (size_t i = 0; i < sizeof(state->match.mode); ++i) {
        HSTEP(state->match.mode[i]);
    }
    HSTEP(state->match.white_to_move);
    HSTEP(state->variant.checkers_must_continue);
    HSTEP(state->moves.selected.file);
    HSTEP(state->moves.selected.rank);
    HSTEP(state->chess.rights.white_can_castle_kingside);
    HSTEP(state->chess.rights.white_can_castle_queenside);
    HSTEP(state->chess.rights.black_can_castle_kingside);
    HSTEP(state->chess.rights.black_can_castle_queenside);
    HSTEP(state->chess.rights.en_passant_target.file);
    HSTEP(state->chess.rights.en_passant_target.rank);
    #undef HSTEP
    return h;
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
        GameState next;
        ai_clone_state(&next, state);
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

static bool ai_compute_move(GameState *state, int depth, AiMove *out_move) {
    if (!state || !out_move) return false;
    if (depth < 1) depth = 1;
    if (depth > 4) depth = 4;

    AiMove moves[AI_MAX_MOVES];
    int count = generate_moves(state, moves, AI_MAX_MOVES);
    if (count <= 0) return false;

    int best_score = -AI_INF;
    int best_index = 0;
    for (int i = 0; i < count; ++i) {
        GameState next;
        ai_clone_state(&next, state);
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

    *out_move = moves[best_index];
    return true;
}

void ai_make_move(GameState *state) {
    if (!state) return;
    if (state->ai.side == AI_SIDE_NONE) return;
    if (state->ui.show_main_menu || state->ui.show_pause_menu) return;
    if (game_is_terminal_phase(state->match.phase)) return;

    bool ai_is_white = state->ai.side == AI_SIDE_WHITE;
    if (state->match.white_to_move != ai_is_white) return;

    int depth = state->ai.depth;
    AiMove best_move = {0};
    if (!ai_compute_move(state, depth, &best_move)) {
        return;
    }

    if (game_apply_move(state, best_move.from, best_move.to, best_move.hint)) {
        if (replay_global_recording()) {
            char promo = '\0';
            if ((best_move.hint.flags & MOVE_PROMOTION) && best_move.hint.promotion) {
                promo = best_move.hint.promotion;
            }
            replay_recorder_record_move(replay_global_recorder(), SDL_GetTicks(),
                best_move.from.file, best_move.from.rank,
                best_move.to.file, best_move.to.rank,
                promo);
        }
    }
}

static int ai_worker_thread(void *userdata) {
    AiWorker *worker = (AiWorker *)userdata;
    while (true) {
        SDL_LockMutex(worker->mutex);
        while (worker->running && !worker->has_job) {
            SDL_CondWait(worker->cond, worker->mutex);
        }
        if (!worker->running) {
            SDL_UnlockMutex(worker->mutex);
            break;
        }
        AiJob job = worker->job;
        worker->has_job = false;
        worker->busy = true;
        SDL_UnlockMutex(worker->mutex);

        uint64_t start = SDL_GetPerformanceCounter();
        AiMove move = {0};
        bool ok = ai_compute_move(&job.state, job.depth, &move);
        uint64_t end = SDL_GetPerformanceCounter();
        double freq = (double)SDL_GetPerformanceFrequency();
        double elapsed_ms = freq > 0.0 ? ((double)(end - start) * 1000.0 / freq) : 0.0;

        SDL_LockMutex(worker->mutex);
        worker->result.from = ok ? move.from : (BoardPos){-1, -1};
        worker->result.to = ok ? move.to : (BoardPos){-1, -1};
        worker->result.hint = ok ? move.hint : (MoveHint){0};
        worker->result.turn_number = job.turn_number;
        worker->result.position_key = job.position_key;
        worker->result.valid = ok;
        worker->has_result = true;
        worker->busy = false;
        worker->last_compute_ms = elapsed_ms;
        SDL_UnlockMutex(worker->mutex);
    }
    return 0;
}

bool ai_worker_init(void) {
    memset(&g_ai_worker, 0, sizeof(g_ai_worker));
    g_ai_worker.mutex = SDL_CreateMutex();
    if (!g_ai_worker.mutex) {
        SDL_Log("AI worker mutex creation failed: %s", SDL_GetError());
        return false;
    }
    g_ai_worker.cond = SDL_CreateCond();
    if (!g_ai_worker.cond) {
        SDL_Log("AI worker cond creation failed: %s", SDL_GetError());
        SDL_DestroyMutex(g_ai_worker.mutex);
        memset(&g_ai_worker, 0, sizeof(g_ai_worker));
        return false;
    }
    g_ai_worker.running = true;
    g_ai_worker.last_compute_ms = 0.0;
    g_ai_worker.thread = SDL_CreateThread(ai_worker_thread, "ai_worker", &g_ai_worker);
    if (!g_ai_worker.thread) {
        SDL_Log("AI worker thread creation failed: %s", SDL_GetError());
        SDL_DestroyCond(g_ai_worker.cond);
        SDL_DestroyMutex(g_ai_worker.mutex);
        memset(&g_ai_worker, 0, sizeof(g_ai_worker));
        return false;
    }
    return true;
}

void ai_worker_shutdown(void) {
    if (!g_ai_worker.mutex) {
        return;
    }
    SDL_LockMutex(g_ai_worker.mutex);
    g_ai_worker.running = false;
    SDL_CondSignal(g_ai_worker.cond);
    SDL_UnlockMutex(g_ai_worker.mutex);
    if (g_ai_worker.thread) {
        SDL_WaitThread(g_ai_worker.thread, NULL);
    }
    if (g_ai_worker.cond) {
        SDL_DestroyCond(g_ai_worker.cond);
    }
    if (g_ai_worker.mutex) {
        SDL_DestroyMutex(g_ai_worker.mutex);
    }
    memset(&g_ai_worker, 0, sizeof(g_ai_worker));
}

bool ai_request_move(const GameState *state) {
    if (!state || !g_ai_worker.mutex) return false;
    SDL_LockMutex(g_ai_worker.mutex);
    if (!g_ai_worker.running || g_ai_worker.has_job || g_ai_worker.busy) {
        SDL_UnlockMutex(g_ai_worker.mutex);
        return false;
    }
    ai_snapshot_state(&g_ai_worker.job.state, state);
    g_ai_worker.job.depth = state->ai.depth;
    g_ai_worker.job.position_key = ai_position_key(state);
    g_ai_worker.job.turn_number = state->match.turn_number;
    g_ai_worker.has_job = true;
    SDL_CondSignal(g_ai_worker.cond);
    SDL_UnlockMutex(g_ai_worker.mutex);
    return true;
}

bool ai_poll_move(AiResult *out) {
    if (!out || !g_ai_worker.mutex) return false;
    SDL_LockMutex(g_ai_worker.mutex);
    if (!g_ai_worker.has_result) {
        SDL_UnlockMutex(g_ai_worker.mutex);
        return false;
    }
    *out = g_ai_worker.result;
    g_ai_worker.has_result = false;
    SDL_UnlockMutex(g_ai_worker.mutex);
    return true;
}

bool ai_get_worker_status(AiWorkerStatus *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!g_ai_worker.mutex) {
        out->ready = false;
        return false;
    }
    SDL_LockMutex(g_ai_worker.mutex);
    out->ready = g_ai_worker.running;
    out->busy = g_ai_worker.busy;
    out->has_job = g_ai_worker.has_job;
    out->has_result = g_ai_worker.has_result;
    out->job_turn_number = g_ai_worker.job.turn_number;
    out->job_position_key = g_ai_worker.job.position_key;
    out->last_compute_ms = g_ai_worker.last_compute_ms;
    SDL_UnlockMutex(g_ai_worker.mutex);
    return true;
}
