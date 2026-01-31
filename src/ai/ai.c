#include "ai/ai.h"
#include "ai/ai_common.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "save/replay.h"
#include "ai/search.h"
#include "core/platform.h"

#define AI_MAX_MOVES 256
#define AI_TIME_LIMIT_MS 10000
#define AI_BUF_PLY (AI_MAX_DEPTH + 8)
#define AI_OVERFLOW_SLOTS 32
#define QS_MAX_DEPTH 24


typedef struct {
    BoardPos from;
    BoardPos to;
    MoveHint hint;
} AiMove;

typedef struct {
    uint32_t key;
    int depth;
    int score;
    TtFlag flag;
    AiMove best;
    bool valid;
} AiTtEntry;

static AiTtEntry g_ai_tt[AI_TT_SIZE];
static uint32_t g_ai_deadline_ms = 0;
static bool g_ai_abort = false;
static uint64_t g_ai_nodes = 0;
static int g_ai_depth_reached = 0;

static THREAD_LOCAL AiMove g_ai_move_buffer[AI_BUF_PLY][AI_MAX_MOVES];
static THREAD_LOCAL GameState g_ai_state_buffer[AI_BUF_PLY];

// Stack pool for overflow allocations beyond AI_BUF_PLY
typedef struct {
    uint8_t *base;
    uint8_t *ptr;
    uint8_t *end;
} StackPool;

static THREAD_LOCAL StackPool g_overflow_moves;
static THREAD_LOCAL StackPool g_overflow_states;
static THREAD_LOCAL bool g_overflow_initialized = false;

static void overflow_pool_init(void) {
    if (g_overflow_initialized) return;

    size_t move_size = sizeof(AiMove) * AI_MAX_MOVES * AI_OVERFLOW_SLOTS;
    size_t state_size = sizeof(GameState) * AI_OVERFLOW_SLOTS;

    g_overflow_moves.base = (uint8_t *)malloc(move_size);
    g_overflow_moves.ptr = g_overflow_moves.base;
    g_overflow_moves.end = g_overflow_moves.base ? g_overflow_moves.base + move_size : NULL;

    g_overflow_states.base = (uint8_t *)malloc(state_size);
    g_overflow_states.ptr = g_overflow_states.base;
    g_overflow_states.end = g_overflow_states.base ? g_overflow_states.base + state_size : NULL;

    g_overflow_initialized = true;
}

static void overflow_pool_shutdown(void) {
    if (!g_overflow_initialized) return;
    free(g_overflow_moves.base);
    free(g_overflow_states.base);
    g_overflow_moves = (StackPool){0};
    g_overflow_states = (StackPool){0};
    g_overflow_initialized = false;
}

static void overflow_pool_reset(void) {
    g_overflow_moves.ptr = g_overflow_moves.base;
    g_overflow_states.ptr = g_overflow_states.base;
}

static void *stack_alloc(StackPool *pool, size_t size) {
    if (!pool->base || pool->ptr + size > pool->end) {
        return malloc(size);  // fallback if pool exhausted
    }
    void *result = pool->ptr;
    pool->ptr += size;
    return result;
}

static void stack_free(StackPool *pool, void *ptr, size_t size) {
    if (!pool->base) {
        free(ptr);
        return;
    }
    // Check if ptr is within pool bounds
    if ((uint8_t *)ptr >= pool->base && (uint8_t *)ptr < pool->end) {
        // Stack free - just decrement pointer
        pool->ptr = (uint8_t *)ptr;
    } else {
        // Was a malloc fallback
        free(ptr);
    }
}

static AiMove *ai_get_move_buf(int ply, bool *out_heap) {
    if (out_heap) *out_heap = false;
    if (ply >= 0 && ply < AI_BUF_PLY) {
        return g_ai_move_buffer[ply];
    }
    if (out_heap) *out_heap = true;
    AiMove *result = (AiMove *)stack_alloc(&g_overflow_moves, sizeof(AiMove) * AI_MAX_MOVES);
    if (!result) {
        SDL_Log("Failed to allocate AI move buffer for ply %d", ply);
    }
    return result;
}

static void ai_free_move_buf(AiMove *buf, bool heap) {
    if (!heap || !buf) return;
    stack_free(&g_overflow_moves, buf, sizeof(AiMove) * AI_MAX_MOVES);
}

static GameState *ai_get_state_buf(int ply, bool *out_heap) {
    if (out_heap) *out_heap = false;
    if (ply >= 0 && ply < AI_BUF_PLY) {
        return &g_ai_state_buffer[ply];
    }
    if (out_heap) *out_heap = true;
    GameState *result = (GameState *)stack_alloc(&g_overflow_states, sizeof(GameState));
    if (!result) {
        SDL_Log("Failed to allocate AI state buffer for ply %d", ply);
    }
    return result;
}

static void ai_free_state_buf(GameState *buf, bool heap) {
    if (!heap || !buf) return;
    stack_free(&g_overflow_states, buf, sizeof(GameState));
}

static inline bool ai_time_up(void) {
    return g_ai_deadline_ms != 0 && SDL_GetTicks() >= g_ai_deadline_ms;
}

typedef struct {
    GameState state;
    int depth;
    uint32_t position_key;
    int turn_number;
    uint32_t generation;
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
    double last_reported_ms;
    uint64_t last_nodes;
    int last_depth;
    uint64_t compute_start_counter;
    double compute_freq;
    AiWorkerStage stage;
    uint32_t generation;
    uint32_t result_generation;
} AiWorker;

static AiWorker g_ai_worker;

static void ai_worker_set_stage(AiWorkerStage stage) {
    if (!g_ai_worker.mutex) return;
    SDL_LockMutex(g_ai_worker.mutex);
    if (g_ai_worker.running) {
        g_ai_worker.stage = stage;
    }
    SDL_UnlockMutex(g_ai_worker.mutex);
}

static void ai_snapshot_state(GameState *dst, const GameState *src) {
    if (!dst || !src) return;
    game_copy_state_minimal(dst, src);
    game_clear_move_flags(dst);
}

// piece_value() is now in ai_common.h

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

static bool ai_move_valid(const AiMove *move) {
    if (!move) return false;
    return move->from.file >= 0 && move->from.file < 8 &&
           move->from.rank >= 0 && move->from.rank < 8 &&
           move->to.file >= 0 && move->to.file < 8 &&
           move->to.rank >= 0 && move->to.rank < 8;
}

static bool ai_move_equals(const AiMove *a, const AiMove *b) {
    if (!a || !b) return false;
    return a->from.file == b->from.file && a->from.rank == b->from.rank &&
           a->to.file == b->to.file && a->to.rank == b->to.rank;
}

static bool ai_move_is_capture(const GameState *state, const AiMove *move) {
    if (!state || !move) return false;
    if (move->hint.flags & MOVE_CAPTURE) return true; // checkers
    if (move->hint.flags & MOVE_EN_PASSANT) return true; // chess
    char target = game_piece_at(state, move->to.file, move->to.rank);
    return target != '.';
}

static int ai_capture_value(const GameState *state, const AiMove *move) {
    if (!state || !move) return 0;
    char target = game_piece_at(state, move->to.file, move->to.rank);
    if (target == '.' && (move->hint.flags & MOVE_EN_PASSANT)) {
        // approximate pawn value for en passant
        return 100;
    }
    return piece_value(target);
}

static int ai_move_score(const GameState *state, const AiMove *move, const AiMove *tt_best) {
    int score = 0;
    if (tt_best && ai_move_valid(tt_best) && ai_move_equals(move, tt_best)) {
        score += 10000;
    }
    if (ai_move_is_capture(state, move)) {
        score += 1000 + ai_capture_value(state, move);
    }
    if (move->hint.flags & MOVE_PROMOTION) {
        score += 500;
    }
    return score;
}

static void ai_order_moves(const GameState *state, AiMove *moves, int count, const AiMove *tt_best) {
    for (int i = 1; i < count; ++i) {
        AiMove cur = moves[i];
        int cur_score = ai_move_score(state, &cur, tt_best);
        int j = i - 1;
        while (j >= 0) {
            int prev_score = ai_move_score(state, &moves[j], tt_best);
            if (prev_score >= cur_score) break;
            moves[j + 1] = moves[j];
            --j;
        }
        moves[j + 1] = cur;
    }
}

static bool ai_tt_probe(uint32_t key, int depth, int alpha, int beta, int *out_score, AiMove *out_best) {
    if (!out_score || !out_best) return false;
    AiTtEntry *entry = &g_ai_tt[key & (AI_TT_SIZE - 1)];
    if (!entry->valid || entry->key != key) {
        return false;
    }
    *out_best = entry->best;
    if (entry->depth < depth) {
        return false;
    }
    if (entry->flag == TT_EXACT) {
        *out_score = entry->score;
        return true;
    }
    if (entry->flag == TT_LOWER && entry->score >= beta) {
        *out_score = entry->score;
        return true;
    }
    if (entry->flag == TT_UPPER && entry->score <= alpha) {
        *out_score = entry->score;
        return true;
    }
    return false;
}

static void ai_tt_store(uint32_t key, int depth, int score, TtFlag flag, const AiMove *best) {
    AiTtEntry *entry = &g_ai_tt[key & (AI_TT_SIZE - 1)];
    if (entry->valid && entry->key == key && entry->depth > depth) {
        return;
    }
    entry->key = key;
    entry->depth = depth;
    entry->score = score;
    entry->flag = flag;
    entry->best = best ? *best : (AiMove){0};
    entry->valid = true;
}


// piece_value() moved to ai_common.h

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

static int quiescence(GameState *state, int alpha, int beta, int ply, int qs_depth) {
    g_ai_nodes++;
    if (g_ai_abort || ai_time_up()) {
        g_ai_abort = true;
        return evaluate(state);
    }
    int stand = evaluate(state);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    // Quiescence depth limit - stop searching captures beyond this depth
    if (qs_depth >= QS_MAX_DEPTH) {
        return stand;
    }

    bool heap_moves = false;
    bool heap_state = false;
    AiMove *moves = ai_get_move_buf(ply, &heap_moves);
    GameState *next = ai_get_state_buf(ply, &heap_state);
    if (!moves || !next) {
        ai_free_move_buf(moves, heap_moves);
        ai_free_state_buf(next, heap_state);
        return stand;
    }
    int count = generate_moves(state, moves, AI_MAX_MOVES);
    if (count <= 0) {
        ai_free_move_buf(moves, heap_moves);
        ai_free_state_buf(next, heap_state);
        return stand;
    }

    ai_order_moves(state, moves, count, NULL);

    for (int i = 0; i < count; ++i) {
        if (!ai_move_is_capture(state, &moves[i])) {
            continue;
        }
        game_copy_state_minimal(next, state);
        if (!game_apply_move(next, moves[i].from, moves[i].to, moves[i].hint)) {
            continue;
        }
        int score = -quiescence(next, -beta, -alpha, ply + 1, qs_depth + 1);
        if (score >= beta) {
            ai_free_move_buf(moves, heap_moves);
            ai_free_state_buf(next, heap_state);
            return beta;
        }
        if (score > alpha) alpha = score;
    }
    ai_free_move_buf(moves, heap_moves);
    ai_free_state_buf(next, heap_state);
    return alpha;
}


static int negamax(GameState *state, int depth, int alpha, int beta, int ply, AiMove *out_best) {
    g_ai_nodes++;
    if (g_ai_abort || ai_time_up()) {
        g_ai_abort = true;
        return evaluate(state);
    }
    int alpha_orig = alpha;
    uint32_t key = ai_position_key(state);
    AiMove tt_best = {0};
    int tt_score = 0;
    bool tt_cut = ai_tt_probe(key, depth, alpha, beta, &tt_score, &tt_best);
    if (tt_cut) {
        if (out_best && ai_move_valid(&tt_best)) {
            *out_best = tt_best;
        }
        return tt_score;
    }

    bool heap_moves = false;
    bool heap_state = false;
    AiMove *moves = ai_get_move_buf(ply, &heap_moves);
    GameState *next = ai_get_state_buf(ply, &heap_state);
    if (!moves || !next) {
        ai_free_move_buf(moves, heap_moves);
        ai_free_state_buf(next, heap_state);
        return evaluate(state);
    }
    int count = generate_moves(state, moves, AI_MAX_MOVES);

    if (count == 0) {
        ai_free_move_buf(moves, heap_moves);
        ai_free_state_buf(next, heap_state);
        return terminal_score(state, depth);
    }
    if (depth == 0) {
        bool checkers = strncmp(state->match.mode, "checkers", 8) == 0;
        int score = checkers ? evaluate(state) : quiescence(state, alpha, beta, ply + 1, 0);
        ai_free_move_buf(moves, heap_moves);
        ai_free_state_buf(next, heap_state);
        return score;
    }

    if (ai_move_valid(&tt_best)) {
        ai_order_moves(state, moves, count, &tt_best);
    } else {
        ai_order_moves(state, moves, count, NULL);
    }

    int best = -AI_INF;
    AiMove best_move = {0};
    bool have_best = false;

    for (int i = 0; i < count; ++i) {
        game_copy_state_minimal(next, state);
        if (!game_apply_move(next, moves[i].from, moves[i].to, moves[i].hint)) {
            continue;
        }
        bool same_turn = next->match.white_to_move == state->match.white_to_move;
        int score = same_turn ? negamax(next, depth - 1, alpha, beta, ply + 1, NULL)
                              : -negamax(next, depth - 1, -beta, -alpha, ply + 1, NULL);
        if (score > best) {
            best = score;
            best_move = moves[i];
            have_best = true;
        }
        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }

    if (out_best && have_best) {
        *out_best = best_move;
    }

    TtFlag flag = TT_EXACT;
    if (best <= alpha_orig) {
        flag = TT_UPPER;
    } else if (best >= beta) {
        flag = TT_LOWER;
    }
    if (have_best) {
        ai_tt_store(key, depth, best, flag, &best_move);
    }

    ai_free_move_buf(moves, heap_moves);
    ai_free_state_buf(next, heap_state);
    return best;
}


static bool ai_compute_move(GameState *state, int depth, AiMove *out_move, bool track_stage) {
    if (!state || !out_move) return false;
    if (depth < 1) depth = 1;
    if (depth > 9) depth = 9;

    // Reset overflow pools for this search
    overflow_pool_reset();

    bool timed = depth >= 9;
    uint32_t deadline = timed ? (SDL_GetTicks() + AI_TIME_LIMIT_MS) : 0;
    int max_depth = timed ? AI_MAX_DEPTH : depth;

    bool checkers = strncmp(state->match.mode, "checkers", 8) == 0;
    if (!checkers) {
        if (track_stage) ai_worker_set_stage(AI_STAGE_SEARCH);
        SearchMove best = {.from = {-1, -1}, .to = {-1, -1}, .hint = {0}};
        bool ok = timed
            ? search_best_move_timed(state, max_depth, deadline, &best)
            : search_best_move(state, depth, &best);
        if (!ok) {
            return false;
        }
        search_get_counters(&g_ai_nodes, &g_ai_depth_reached);
        out_move->from = best.from;
        out_move->to = best.to;
        out_move->hint = best.hint;
        return true;
    }

    g_ai_deadline_ms = deadline;
    g_ai_abort = false;
    g_ai_nodes = 0;
    g_ai_depth_reached = 0;

    AiMove best_move = {0};
    bool found = false;

    for (int d = 1; d <= max_depth; ++d) {
        if (timed && ai_time_up()) break;
        if (track_stage) ai_worker_set_stage(AI_STAGE_SEARCH);
        AiMove iter_best = {0};
        int score = negamax(state, d, -AI_INF, AI_INF, 0, &iter_best);
        (void)score;
        if (g_ai_abort) break;
        if (ai_move_valid(&iter_best)) {
            best_move = iter_best;
            found = true;
            g_ai_depth_reached = d;
        }
    }

    g_ai_deadline_ms = 0;
    if (!found) {
        return false;
    }
    *out_move = best_move;
    return true;
}


bool ai_make_move(GameState *state) {
    if (!state) return false;
    if (state->ai.side == AI_SIDE_NONE) return false;
    if (state->ui.show_main_menu || state->ui.show_pause_menu) return false;
    if (game_is_terminal_phase(state->match.phase)) return false;

    bool ai_is_white = state->ai.side == AI_SIDE_WHITE;
    if (state->match.white_to_move != ai_is_white) return false;

    int depth = state->ai.depth;
    AiMove best_move = {0};
    if (!ai_compute_move(state, depth, &best_move, false)) {
        return false;
    }

    if (game_apply_move_ai(state, best_move.from, best_move.to, best_move.hint)) {
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
        return true;
    }
    return false;
}

static int ai_worker_thread(void *userdata) {
    AiWorker *worker = (AiWorker *)userdata;
    overflow_pool_init();  // Initialize thread-local pools for this worker
    while (true) {
        SDL_LockMutex(worker->mutex);
        while (worker->running && !worker->has_job) {
            SDL_CondWait(worker->cond, worker->mutex);
        }
        if (!worker->running) {
            SDL_UnlockMutex(worker->mutex);
            break;
        }
        AiJob *job = &worker->job;
        worker->has_job = false;
        worker->busy = true;
        worker->stage = AI_STAGE_GENERATE;
        worker->compute_start_counter = SDL_GetPerformanceCounter();
        SDL_UnlockMutex(worker->mutex);

        uint64_t start = worker->compute_start_counter;
        AiMove move = {0};
        bool ok = ai_compute_move(&job->state, job->depth, &move, true);
        uint64_t end = SDL_GetPerformanceCounter();
        double freq = worker->compute_freq;
        double elapsed_ms = freq > 0.0 ? ((double)(end - start) * 1000.0 / freq) : 0.0;

        SDL_LockMutex(worker->mutex);
        worker->result.from = ok ? move.from : (BoardPos){-1, -1};
        worker->result.to = ok ? move.to : (BoardPos){-1, -1};
        worker->result.hint = ok ? move.hint : (MoveHint){0};
        worker->result.turn_number = job->turn_number;
        worker->result.position_key = job->position_key;
        worker->result.valid = ok;
        worker->has_result = true;
        worker->result_generation = job->generation;
        worker->busy = false;
        worker->last_compute_ms = elapsed_ms;
        worker->last_reported_ms = elapsed_ms;
        worker->last_nodes = g_ai_nodes;
        worker->last_depth = g_ai_depth_reached;
        worker->stage = AI_STAGE_DONE;
        SDL_UnlockMutex(worker->mutex);
    }
    overflow_pool_shutdown();  // Clean up thread-local pools
    return 0;
}

bool ai_worker_init(void) {
    overflow_pool_init();
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
    g_ai_worker.last_reported_ms = 0.0;
    g_ai_worker.compute_start_counter = 0;
    g_ai_worker.compute_freq = (double)SDL_GetPerformanceFrequency();
    g_ai_worker.stage = AI_STAGE_IDLE;
    g_ai_worker.generation = 0;
    g_ai_worker.result_generation = 0;
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
    // Interrupt any in-progress search before waiting for thread
    g_ai_abort = true;
    search_abort();
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
    overflow_pool_shutdown();
}

void ai_worker_reset(void) {
    if (!g_ai_worker.mutex) {
        return;
    }
    g_ai_abort = true;
    SDL_LockMutex(g_ai_worker.mutex);
    g_ai_worker.generation++;
    g_ai_worker.has_job = false;
    g_ai_worker.has_result = false;
    g_ai_worker.stage = g_ai_worker.busy ? g_ai_worker.stage : AI_STAGE_IDLE;
    SDL_UnlockMutex(g_ai_worker.mutex);
}

bool ai_request_move(const GameState *state) {
    if (!state || !g_ai_worker.mutex) return false;
    SDL_LockMutex(g_ai_worker.mutex);
    if (!g_ai_worker.running || g_ai_worker.has_job || g_ai_worker.busy || g_ai_worker.has_result) {
        SDL_UnlockMutex(g_ai_worker.mutex);
        return false;
    }
    ai_snapshot_state(&g_ai_worker.job.state, state);
    g_ai_worker.job.depth = state->ai.depth;
    g_ai_worker.job.position_key = ai_position_key(state);
    g_ai_worker.job.turn_number = state->match.turn_number;
    g_ai_worker.job.generation = g_ai_worker.generation;
    g_ai_worker.has_job = true;
    g_ai_worker.stage = AI_STAGE_QUEUED;
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
    if (g_ai_worker.result_generation != g_ai_worker.generation) {
        g_ai_worker.has_result = false;
        SDL_UnlockMutex(g_ai_worker.mutex);
        return false;
    }
    *out = g_ai_worker.result;
    g_ai_worker.has_result = false;
    if (!g_ai_worker.busy && !g_ai_worker.has_job) {
        g_ai_worker.stage = AI_STAGE_IDLE;
    }
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
    if (g_ai_worker.busy) {
        double freq = g_ai_worker.compute_freq;
        uint64_t now = SDL_GetPerformanceCounter();
        if (freq > 0.0 && now >= g_ai_worker.compute_start_counter) {
            out->last_compute_ms = ((double)(now - g_ai_worker.compute_start_counter) * 1000.0 / freq);
        } else {
            out->last_compute_ms = 0.0;
        }
        // Read live counters during search (minor data race acceptable for display)
        // Chess uses search.c counters, checkers uses g_ai_nodes/g_ai_depth_reached
        bool checkers = strncmp(g_ai_worker.job.state.match.mode, "checkers", 8) == 0;
        out->last_nodes = checkers ? g_ai_nodes : search_get_live_nodes();
        out->last_depth = checkers ? g_ai_depth_reached : search_get_live_depth();
    } else {
        out->last_compute_ms = g_ai_worker.last_compute_ms;
        out->last_nodes = g_ai_worker.last_nodes;
        out->last_depth = g_ai_worker.last_depth;
    }
    out->stage = g_ai_worker.stage;
    SDL_UnlockMutex(g_ai_worker.mutex);
    return true;
}
