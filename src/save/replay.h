#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game/logic.h"

typedef enum {
    REPLAY_EVENT_HOVER = 1,
    REPLAY_EVENT_CLICK = 2,
    REPLAY_EVENT_EXPECT_ILLEGAL = 3,
    REPLAY_EVENT_ASSERT_TURN = 4,
    REPLAY_EVENT_ASSERT_PHASE = 5,
    REPLAY_EVENT_ASSERT_CASTLE = 6,
    REPLAY_EVENT_ASSERT_EN_PASSANT = 7,
    REPLAY_EVENT_ASSERT_LEGAL = 8,
} ReplayEventType;
// Event text format (one per line after "events"):
//   t hover file rank
//   t click file rank
//   t expect_illegal_move file rank      // click that must NOT change state
//   t assert_turn N                      // enforce turn_number
//   t assert_phase PHASE                 // enforce game_phase string
//   t assert_castle KQkq                 // enforce castling rights
//   t assert_en_passant file rank        // enforce en passant target
//   t assert_legal sel_file sel_rank allowed=f:r,f:r forbidden=f:r   // enforce legal move set for selected piece (use file:rank pairs, optional, use "-" to skip)

typedef struct {
    uint32_t t_ms;
    ReplayEventType type;
    int8_t file;
    int8_t rank;
    int8_t aux_file;
    int8_t aux_rank;
    int32_t arg;
    char text[24];
    int8_t allowed_files[64];
    int8_t allowed_ranks[64];
    int allowed_count;
    int8_t forbidden_files[64];
    int8_t forbidden_ranks[64];
    int forbidden_count;
} ReplayEvent;

typedef struct {
    char game_mode[16];
    char game_phase[24];
    bool white_to_move;
    int turn_number;
    bool white_can_castle_kingside;
    bool white_can_castle_queenside;
    bool black_can_castle_kingside;
    bool black_can_castle_queenside;
    BoardPos en_passant_target;
    char board[8][9]; // null-terminated rows
    char promote_white; // desired promotion piece (uppercase)
    char promote_black; // desired promotion piece (lowercase)
} ReplayStartState;

typedef struct {
    ReplayStartState start;
    ReplayEvent *events;
    size_t event_count;
    size_t event_capacity;
    uint32_t start_ticks;
    bool recording;
} ReplayRecorder;

typedef struct {
    ReplayStartState start;
    ReplayEvent *events;
    size_t event_count;
    size_t event_capacity;
    // Optional expectations for testing
    bool has_expect_phase;
    char expect_phase[24];
    bool has_expect_turn;
    int expect_turn;
    bool has_expect_checksum;
    uint32_t expect_checksum;
    bool has_expect_board;
    char expect_board[8][9]; // rows, null-terminated
} ReplayData;

void replay_recorder_begin(ReplayRecorder *rec, const GameState *state, uint32_t now_ticks);
void replay_recorder_record_hover(ReplayRecorder *rec, uint32_t now_ticks, int file, int rank);
void replay_recorder_record_click(ReplayRecorder *rec, uint32_t now_ticks, int file, int rank);
bool replay_recorder_save(const ReplayRecorder *rec, const char *path);
void replay_recorder_end(ReplayRecorder *rec);

// Optional global recorder helpers (for input hooks)
bool replay_global_recording(void);
ReplayRecorder *replay_global_recorder(void);
void replay_global_start(const GameState *state, uint32_t now_ticks);
bool replay_global_save(const char *path);
void replay_global_stop(void);

bool replay_load(const char *path, ReplayData *out);
void replay_free(ReplayData *data);
bool replay_seed_game(const ReplayStartState *start, GameState *state);
bool replay_apply_events(const ReplayData *data, GameState *state, bool apply_hover);
bool replay_apply_event(const ReplayEvent *ev, GameState *state, bool apply_hover, bool enforce_asserts);

uint32_t replay_checksum(const GameState *state);
