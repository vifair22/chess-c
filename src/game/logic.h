#pragma once

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

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
    int file; // 0-7, -1 when unset
    int rank; // 0-7, -1 when unset
} Selection;

typedef struct {
    Selection selected;
    BoardPos hover_pos; // current hover/preview square
    MoveHint legal_moves[8][8]; // metadata for legal destinations
} MoveContext;

typedef struct {
    bool white_can_castle_kingside;
    bool white_can_castle_queenside;
    bool black_can_castle_kingside;
    bool black_can_castle_queenside;
    BoardPos en_passant_target; // square that can be captured en passant, -1/-1 if none
    int halfmove_clock; // half-move counter for fifty-move rule
} ChessRights;

typedef struct {
    uint64_t hashes[512]; // recent position hashes for repetition
    int len;
} PositionHistory;

typedef struct {
    ChessRights rights;
    PositionHistory history;
} ChessRulesState;

typedef struct {
    BoardPos start;
    BoardPos end;
} LastMove;

typedef struct {
    BoardPos from;
    BoardPos to;
    char piece;
    char promotion;
    bool white_move;
    char san[16];
} MoveRecord;

typedef struct {
    bool show_main_menu;
    bool show_pause_menu;
    bool show_debug;
    bool undo_used;
    bool show_save_modal;
    bool show_load_menu;
    bool show_load_confirm;
    bool show_new_menu;
    bool load_needs_refresh;
    bool load_is_replay;
    int main_menu_index;
    int new_menu_index;
    int pause_menu_index;
    int save_menu_index;
    int load_menu_focus;
    int load_confirm_index;
    bool menu_keyboard_active;
    bool menu_mouse_active;
    char save_name[64];
    int save_len;
    char load_names[32][64];
    time_t load_times[32];
    int load_count;
    int load_scroll;
    int load_selected;
    bool load_scroll_dragging;
    int load_scroll_drag_offset;
    int turn_log_scroll;
    int turn_log_max_scroll;
    bool replay_mode_active;
    bool replay_request_pending;
    char replay_request[256];
    bool ai_highlight_active;
    bool ai_highlight_suppressed;
    BoardPos ai_highlight_from;
    BoardPos ai_highlight_to;
  } UiState;

typedef struct {
    double last_frame_seconds; // duration of last frame/tick
    double last_fps;           // frames (ticks) per second
    double smoothed_frame_ms;
    double smoothed_fps;
    double accum_time;
    int accum_frames;
} PerfState;

struct GameSnapshot;

typedef enum {
    AI_SIDE_NONE = 0,
    AI_SIDE_WHITE = 1,
    AI_SIDE_BLACK = 2,
} AiSide;

typedef struct {
    AiSide side;
    int depth; // search depth (1-8), 9 = timed (10s) search
} AiSettings;

typedef struct {
    char mode[16]; // e.g., "chess", "checkers"
    char phase[24]; // e.g., "play", "checkmate", "win_white_resign"
    bool white_to_move;
    int turn_number;
    GameTime game_time;
} MatchState;

typedef struct {
    bool checkers_must_continue; // true when a multi-jump is in progress
    char promotion_choice_white; // desired promotion piece for white (uppercase)
    char promotion_choice_black; // desired promotion piece for black (lowercase)
} VariantState;

typedef struct {
    SDL_mutex *mutex;
    BoardState board_state;
    MatchState match;
    MoveContext moves;
    LastMove last_move;
    VariantState variant;
    ChessRulesState chess;
    char white_captures[32];
    int white_captures_len;
    char black_captures[32];
    int black_captures_len;
    MoveRecord move_history[512];
    int move_history_len;
    UiState ui;
    PerfState perf;
    AiSettings ai;
    double ai_last_move_ms;
    bool ai_timing_active;
    uint64_t ai_timing_start;
    double ai_time_freq;
    struct GameSnapshot *snapshots;
    int snapshot_len;
    int snapshot_cap;
    int autosave_last_turn;
    bool autosave_terminal_saved;
} GameState;

bool game_init(GameState *state);
void game_destroy(GameState *state);
bool game_is_white_piece(char piece);
bool game_is_black_piece(char piece);
bool game_is_terminal_phase(const char *phase);
bool game_square_attacked_by(const GameState *state, int file, int rank, bool by_white);
bool game_is_in_check(const GameState *state, bool white);
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
// Copy the minimal fields needed for move generation and legality checks.
void game_copy_state_minimal(GameState *dst, const GameState *src);
// Apply a legal move (used by AI/replay), returns false if invalid or unsupported.
bool game_apply_move(GameState *state, BoardPos from, BoardPos to, MoveHint hint);
// Apply a legal move for the AI turn (bypasses AI turn input blocking). Asserts on invalid AI moves.
bool game_apply_move_ai(GameState *state, BoardPos from, BoardPos to, MoveHint hint);
// Undo the last completed turn (returns false if no snapshot available).
bool game_undo(GameState *state);
// Undo with optional animation update (false keeps last_move unchanged).
bool game_undo_ex(GameState *state, bool update_anim);
// Peek the next undo snapshot's side to move.
bool game_peek_undo_white_to_move(const GameState *state, bool *out_white_to_move);
