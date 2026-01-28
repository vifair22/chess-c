#pragma once

#include "game/logic.h"

typedef struct {
    BoardPos from;
    BoardPos to;
    MoveHint hint;
    int turn_number;
    uint32_t position_key;
    bool valid;
} AiResult;

typedef enum {
    AI_STAGE_IDLE = 0,
    AI_STAGE_QUEUED,
    AI_STAGE_GENERATE,
    AI_STAGE_SEARCH,
    AI_STAGE_DONE
} AiWorkerStage;

typedef struct {
    bool ready;
    bool busy;
    bool has_job;
    bool has_result;
    int job_turn_number;
    uint32_t job_position_key;
    double last_compute_ms;
    AiWorkerStage stage;
} AiWorkerStatus;

bool ai_worker_init(void);
void ai_worker_shutdown(void);
bool ai_request_move(const GameState *state);
bool ai_poll_move(AiResult *out);
uint32_t ai_position_key(const GameState *state);
bool ai_get_worker_status(AiWorkerStatus *out);

bool ai_make_move(GameState *state);
