#pragma once

#include <stdbool.h>

#include "game/logic.h"

bool save_refresh_list(GameState *state);
bool save_write_named(GameState *state, const char *name);
bool save_load_named(GameState *state, const char *filename);
bool replay_refresh_list(GameState *state);
bool replay_write_named(GameState *state, const char *name);
bool replay_request_named(GameState *state, const char *filename);
