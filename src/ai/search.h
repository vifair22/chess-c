#pragma once

#include <stdint.h>
#include "game/logic.h"

typedef struct {
    BoardPos from;
    BoardPos to;
    MoveHint hint;
} SearchMove;

bool search_best_move(const GameState *state, int depth, SearchMove *out_move);
bool search_best_move_timed(const GameState *state, int max_depth, uint32_t deadline_ms, SearchMove *out_move);
void search_reset_counters(void);
void search_get_counters(uint64_t *out_nodes, int *out_depth);
