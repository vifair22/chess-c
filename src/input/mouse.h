#pragma once

#include <SDL.h>

#include "game/logic.h"
#include "renderer/render.h"

void mouse_handle_event(GameState *game, const RenderContext *render, const SDL_Event *event);
