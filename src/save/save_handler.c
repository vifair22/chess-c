#include "save/save_handler.h"

#include <SDL.h>
#include <ctype.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

#include "save/replay.h"

static void ensure_saves_dir(void) {
    if (_mkdir("saves") != 0) {
        if (errno != EEXIST) {
            SDL_Log("Failed to create saves directory (errno=%d)", errno);
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
    if (len == 0 && out_cap > 1) {
        out[len++] = 's';
    }
    out[len] = '\0';
}

bool save_refresh_list(GameState *state) {
    if (!state) return false;
    ensure_saves_dir();
    state->ui.load_count = 0;
    state->ui.load_scroll = 0;

    struct _finddata_t entry;
    intptr_t handle = _findfirst("saves\\*.replay", &entry);
    if (handle == -1) {
        return true;
    }
    do {
        if (state->ui.load_count >= (int)(sizeof(state->ui.load_names) / sizeof(state->ui.load_names[0]))) {
            break;
        }
        strncpy(state->ui.load_names[state->ui.load_count], entry.name, sizeof(state->ui.load_names[0]) - 1);
        state->ui.load_names[state->ui.load_count][sizeof(state->ui.load_names[0]) - 1] = '\0';
        state->ui.load_count += 1;
    } while (_findnext(handle, &entry) == 0);
    _findclose(handle);
    return true;
}

bool save_write_named(GameState *state, const char *name) {
    if (!state || !name || !name[0]) return false;
    ensure_saves_dir();

    char sanitized[96];
    sanitize_name(name, sanitized, sizeof(sanitized));
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
    replay_apply_events(&replay, state, false);
    replay_free(&replay);

    state->ui.show_main_menu = false;
    state->ui.show_pause_menu = false;
    state->ui.show_save_modal = false;
    state->ui.show_load_menu = false;
    state->ui.show_load_confirm = false;
    state->ui.load_needs_refresh = false;
    state->ui.show_new_menu = false;

    replay_global_stop();
    replay_global_start(state, SDL_GetTicks());

    state->autosave_last_turn = -1;
    state->autosave_terminal_saved = false;

    return true;
}
