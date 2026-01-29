#include "save/save_handler.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "save/replay.h"
#include "core/filesystem.h"

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
    if (!fs_mkdir("saves")) {
        SDL_Log("Failed to create saves directory");
    }
}

static void ensure_replays_dir(void) {
    if (!fs_mkdir("replays")) {
        SDL_Log("Failed to create replays directory");
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

    FsFileList file_list;
    if (!fs_list_files(pattern, &file_list)) {
        return true; // Empty list is OK
    }

    int max_files = (int)(sizeof(state->ui.load_names) / sizeof(state->ui.load_names[0]));
    for (int i = 0; i < file_list.count && state->ui.load_count < max_files; ++i) {
        FsFileEntry *entry = &file_list.entries[i];
        if (entry->is_directory) continue; // Skip directories

        strncpy(state->ui.load_names[state->ui.load_count], entry->name, sizeof(state->ui.load_names[0]) - 1);
        state->ui.load_names[state->ui.load_count][sizeof(state->ui.load_names[0]) - 1] = '\0';
        state->ui.load_times[state->ui.load_count] = entry->modified_time;
        state->ui.load_count += 1;
    }

    fs_file_list_free(&file_list);

    // Sort by modification time (newest first)
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
    return refresh_list(state, "saves/*.save");
}

bool replay_refresh_list(GameState *state) {
    if (!state) return false;
    ensure_replays_dir();
    return refresh_list(state, "replays/*.replay");
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
    snprintf(path, sizeof(path), "saves/%s.save", sanitized);
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
        return false;
    }
    replay_global_load(&replay, SDL_GetTicks());
    replay_free(&replay);

    state->ui.show_main_menu = false;
    state->ui.show_pause_menu = false;
    state->ui.show_save_modal = false;
    state->ui.show_load_menu = false;
    state->ui.show_load_confirm = false;
    state->ui.load_needs_refresh = false;
    state->ui.show_new_menu = false;
    state->ui.replay_mode_active = false;

    return true;
}

bool replay_write_named(GameState *state, const char *name) {
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
    ensure_replays_dir();

    char sanitized[96];
    sanitize_name(name, sanitized, sizeof(sanitized));
    if (!sanitized[0]) {
        return false;
    }
    char path[256];
    snprintf(path, sizeof(path), "replays/%s.replay", sanitized);
    return replay_global_save(path);
}

bool replay_request_named(GameState *state, const char *filename) {
    if (!state || !filename || !filename[0]) return false;
    ensure_replays_dir();
    snprintf(state->ui.replay_request, sizeof(state->ui.replay_request), "replays/%s", filename);
    state->ui.replay_request_pending = true;
    return true;
}
