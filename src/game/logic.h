#pragma once

#include <SDL_mutex.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char piece;    // '.' when empty, otherwise piece code
    uint8_t flags; // bitmask: see SquareFlags
} SquareState;

typedef struct {
    int file; // 0-7, -1 when unset
    int rank; // 0-7, -1 when unset
} BoardPos;

typedef struct {
    SquareState squares[8][8];
} BoardState;

typedef struct {
    double total_seconds; // accumulated game time
} GameTime;

enum SquareFlags {
    SQUARE_BLOCKED = 1 << 0,  // square blocked for attacks
    SQUARE_LEGAL = 1 << 1,    // legal destination for the current move
    SQUARE_HIGHLIGHT = 1 << 2, // generic highlight (e.g., hover/preview)
    SQUARE_CHECK = 1 << 3      // king in check indicator
};

typedef struct {
    uint8_t flags; // bitmask: see MoveFlags
    char promotion; // promotion piece when applicable (uppercase)
} MoveHint;

enum MoveFlags {
    MOVE_LEGAL = 1 << 0,
    MOVE_EN_PASSANT = 1 << 1,
    MOVE_CASTLE_KING = 1 << 2,
    MOVE_CASTLE_QUEEN = 1 << 3,
    MOVE_PROMOTION = 1 << 4,
    MOVE_CAPTURE = 1 << 5, // generic capture (checkers)
    MOVE_CHAIN = 1 << 6    // move keeps the turn (checkers multi-jump)
};

typedef struct {
    SDL_mutex *mutex;
    BoardState board_state;
    MoveHint legal_moves[8][8]; // metadata for legal destinations
    bool white_to_move;
    int selected_file; // 0-7, -1 when nothing selected
    int selected_rank; // 0-7, -1 when nothing selected
    BoardPos hover_pos; // current hover/preview square
    char game_mode[16]; // e.g., "chess", "checkers", "ataxx"
    int turn_number;
    GameTime game_time;
    char game_phase[24]; // e.g., "play", "checkmate", "win_white_resign"
    bool white_can_castle_kingside;
    bool white_can_castle_queenside;
    bool black_can_castle_kingside;
    bool black_can_castle_queenside;
    BoardPos en_passant_target; // square that can be captured en passant, -1/-1 if none
    BoardPos move_start;
    BoardPos move_end;
    bool checkers_must_continue; // true when a multi-jump is in progress
    bool show_main_menu;
    bool show_pause_menu;
    double last_frame_seconds; // duration of last frame/tick
    double last_fps;           // frames (ticks) per second
    double smoothed_frame_ms;
    double smoothed_fps;
    double perf_accum_time;
    int perf_accum_frames;
    bool show_debug;
    char promotion_choice_white; // desired promotion piece for white (uppercase)
    char promotion_choice_black; // desired promotion piece for black (lowercase)
    int halfmove_clock; // half-move counter for fifty-move rule
    uint64_t position_history[512]; // recent position hashes for repetition
    int position_history_len;
} GameState;

bool game_init(GameState *state);
void game_destroy(GameState *state);
// Resign for the current side to move.
void game_resign(GameState *state);
void game_handle_click(GameState *state, int file, int rank);
char game_piece_at(const GameState *state, int file, int rank);
void game_eval_legal_moves(GameState *state, int file, int rank);
void game_clear_move_flags(GameState *state);
void game_reset(GameState *state, const char *mode);
// Recompute check/stalemate/checkmate phase after seeding or external board changes.
void game_recompute_phase(GameState *state);
// Reset repetition history (e.g., after seeding a custom board state).
void game_reset_history(GameState *state);
