#pragma once

#include "game/logic.h"

typedef struct {
    BoardPos from;
    BoardPos to;
    MoveHint hint;
} SearchMove;

bool search_best_move(const GameState *state, int depth, SearchMove *out_move);
