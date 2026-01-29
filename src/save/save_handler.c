#include "save/save_handler.h"

#include <SDL.h>
#include <ctype.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

#include "save/replay.h"

static const char *replay_event_type_label(ReplayEventType type) {
    switch (type) {
        case REPLAY_EVENT_HOVER: return "hover";
        case REPLAY_EVENT_CLICK: return "click";
        case REPLAY_EVENT_MOVE: return "move";
        case REPLAY_EVENT_EXPECT_ILLEGAL: return "expect_illegal";
        case REPLAY_EVENT_ASSERT_TURN: return "assert_turn";
        case REPLAY_EVENT_ASSERT_PHASE: return "assert_phase";
        case REPLAY_EVENT_ASSERT_CASTLE: return "assert_castle";
        case REPLAY_EVENT_ASSERT_EN_PASSANT: return "assert_en_passant";
        case REPLAY_EVENT_ASSERT_LEGAL: return "assert_legal";
        default: return "unknown";
    }
}

static void ensure_saves_dir(void) {
    if (_mkdir("saves") != 0) {
        if (errno != EEXIST) {
            SDL_Log("Failed to create saves directory (errno=%d)", errno);
        }
    }
}

static void ensure_replays_dir(void) {
    if (_mkdir("replays") != 0) {
        if (errno != EEXIST) {
            SDL_Log("Failed to create replays directory (errno=%d)", errno);
        }
    }
}

static void sanitize_name(const char *name, char *out, size_t out_cap) {
    size_t len = 0;
    for (const char *p = name; *p && len + 1 < out_cap; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '-' || c == '_' || c == ' ') {
            out[len++] = (c == ' ') ? '_' : (char)c;
        }
    }
    out[len] = '\0';
}

static bool refresh_list(GameState *state, const char *pattern) {
    if (!state || !pattern) return false;
    state->ui.load_count = 0;
    state->ui.load_scroll = 0;

    struct _finddata_t entry;
    intptr_t handle = _findfirst(pattern, &entry);
    if (handle == -1) {
        return true;
    }
    do {
        if (state->ui.load_count >= (int)(sizeof(state->ui.load_names) / sizeof(state->ui.load_names[0]))) {
            break;
        }
        strncpy(state->ui.load_names[state->ui.load_count], entry.name, sizeof(state->ui.load_names[0]) - 1);
        state->ui.load_names[state->ui.load_count][sizeof(state->ui.load_names[0]) - 1] = '\0';
        state->ui.load_times[state->ui.load_count] = entry.time_write;
        state->ui.load_count += 1;
    } while (_findnext(handle, &entry) == 0);
    _findclose(handle);

    for (int i = 0; i < state->ui.load_count - 1; ++i) {
        int best = i;
        for (int j = i + 1; j < state->ui.load_count; ++j) {
            if (state->ui.load_times[j] > state->ui.load_times[best]) {
                best = j;
            }
        }
        if (best != i) {
            char tmp_name[64];
            time_t tmp_time;
            memcpy(tmp_name, state->ui.load_names[i], sizeof(tmp_name));
            memcpy(state->ui.load_names[i], state->ui.load_names[best], sizeof(state->ui.load_names[i]));
            memcpy(state->ui.load_names[best], tmp_name, sizeof(state->ui.load_names[best]));
            tmp_time = state->ui.load_times[i];
            state->ui.load_times[i] = state->ui.load_times[best];
            state->ui.load_times[best] = tmp_time;
        }
    }
    return true;
}

bool save_refresh_list(GameState *state) {
    if (!state) return false;
    ensure_saves_dir();
    return refresh_list(state, "saves\\*.replay");
}

bool replay_refresh_list(GameState *state) {
    if (!state) return false;
    ensure_replays_dir();
    return refresh_list(state, "replays\\*.replay");
}

bool save_write_named(GameState *state, const char *name) {
    if (!state || !name || !name[0]) return false;
    bool has_non_space = false;
    for (const char *p = name; *p; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            has_non_space = true;
            break;
        }
    }
    if (!has_non_space) {
        return false;
    }
    ensure_saves_dir();

    char sanitized[96];
    sanitize_name(name, sanitized, sizeof(sanitized));
    if (!sanitized[0]) {
        return false;
    }
    char path[256];
    snprintf(path, sizeof(path), "saves/%s.replay", sanitized);
    return replay_global_save(path);
}

bool save_load_named(GameState *state, const char *filename) {
    if (!state || !filename || !filename[0]) return false;
    ensure_saves_dir();

    char path[256];
    snprintf(path, sizeof(path), "saves/%s", filename);

    ReplayData replay = {0};
    if (!replay_load(path, &replay)) {
        SDL_Log("Failed to load save: %s", path);
        return false;
    }
    replay_seed_game(&replay.start, state);
    bool ok = true;
    for (size_t i = 0; i < replay.event_count; ++i) {
        const ReplayEvent *ev = &replay.events[i];
        if (!replay_apply_event(ev, state, false, true)) {
            SDL_Log("Failed to apply save events: %s (event %zu, type=%s, t=%u, file=%d:%d aux=%d:%d arg=%d text=%s, phase=%s turn=%d)",
                path, i, replay_event_type_label(ev->type), ev->t_ms,
                ev->file, ev->rank, ev->aux_file, ev->aux_rank, ev->arg, ev->text,
                state->match.phase, state->match.turn_number);
            ok = false;
            break;
        }
    }
    if (ok && replay.has_expect_phase) {
        strncpy(state->match.phase, replay.expect_phase, sizeof(state->match.phase) - 1);
        state->match.phase[sizeof(state->match.phase) - 1] = '\0';
    }
    if (!ok) {
        replay_free(&replay);
        game_reset(state, state->match.mode);
        state->ui.show_main_menu = true;
        state->ui.show_pause_menu = false;
        state->ui.show_save_modal = false;
        state->ui.show_load_menu = false;
        state->ui.show_load_confirm = false;
        state->ui.load_needs_refresh = false;
        state->ui.show_new_menu = false;
        replay_global_stop();
        return false;
    }
    replay_free(&replay);

    state->ui.show_main_menu = false;
    state->ui.show_pause_menu = false;
    state->ui.show_save_modal = false;
    state->ui.show_load_menu = false;
    state->ui.show_load_confirm = false;
    state->ui.load_needs_refresh = false;
    state->ui.show_new_menu = false;
    state->ui.replay_mode_active = false;

    replay_global_stop();
    replay_global_start(state, SDL_GetTicks());

    state->autosave_last_turn = -1;
    state->autosave_terminal_saved = false;

    return true;
}

bool replay_request_named(GameState *state, const char *filename) {
    if (!state || !filename || !filename[0]) return false;
    ensure_replays_dir();
    snprintf(state->ui.replay_request, sizeof(state->ui.replay_request), "replays/%s", filename);
    state->ui.replay_request_pending = true;
    return true;
}
