#pragma once

#include "game/logic.h"
#include "core/platform.h"

// Forward declarations
typedef struct GameSnapshot GameSnapshot;

// Internal shared state
extern THREAD_LOCAL GameState g_logic_tmp_primary;
extern THREAD_LOCAL GameState g_logic_tmp_secondary;

// GameSnapshot structure (internal to game module)
struct GameSnapshot {
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
};

// === Shared utilities ===
bool in_bounds(int file, int rank);
SquareState *board_square_ptr(BoardState *board, int file, int rank);
bool game_mode_is(const GameState *state, const char *mode);

// === State management (state.c) ===
void snapshot_store(GameSnapshot *dst, const GameState *state);
void snapshot_push(GameState *state);
uint64_t position_hash(const GameState *state);
void record_position(GameState *state);
void reset_position_history(GameState *state);
int repetition_count(const GameState *state);
bool is_insufficient_material(const GameState *state);

// === Move utilities (moves.c) ===
void record_capture(GameState *state, char capturer, char captured);
void build_san(const GameState *state, BoardPos from, BoardPos to, MoveHint hint,
    char moving, char captured, char *out, size_t cap);
void record_move(GameState *state, BoardPos from, BoardPos to, char piece, char promotion,
    bool white_move, MoveHint hint, char captured);
void apply_basic_move(GameState *state, int from_file, int from_rank, int to_file, int to_rank,
    MoveHint hint, bool update_counters);
void init_board_chess(GameState *state);
void init_board_checkers(GameState *state);
void init_board_for_mode(GameState *state);

// === Chess rules (chess_rules.c) ===
BoardPos find_king(const GameState *state, bool white);
bool mark_check_path(GameState *state, int kf, int kr, bool by_white);
bool has_any_legal_move(const GameState *state, bool white_to_move);
void update_game_phase_chess(GameState *state);
void game_eval_legal_moves_chess(GameState *state, int file, int rank);
void apply_chess_move_common(GameState *state, BoardPos from, BoardPos to, MoveHint hint);

// === Checkers rules (checkers_rules.c) ===
bool checkers_is_king(char piece);
bool checkers_can_capture_from(const GameState *state, int file, int rank, char piece);
bool checkers_player_has_capture(const GameState *state, bool white);
bool checkers_player_has_any_move(const GameState *state, bool white);
void update_game_phase_checkers(GameState *state);
void game_eval_legal_moves_checkers(GameState *state, int file, int rank);
void apply_checkers_move_common(GameState *state, int from_file, int from_rank,
    int to_file, int to_rank, MoveHint hint, bool snapshot);
