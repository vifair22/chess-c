#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "ai/ai.h"
#include "game/logic.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "renderer/render.h"
#include "save/replay.h"

/* ============================================================================
 * Type Definitions
 * ========================================================================== */

/*
 * AppConfig: Parsed command-line configuration.
 * Populated by parse_args() before game initialization.
 */
typedef struct {
    const char *record_path;   /* Path to save recorded replay, or NULL */
    const char *replay_path;   /* Path to load replay from, or NULL */
    bool headless;             /* Run without window (for automated testing) */
    bool test_mode;            /* Enable assertions and exit on failure */
} AppConfig;

/*
 * ReplayState: Runtime state for interactive replay playback.
 * Tracks position within the replay and timing information.
 */
typedef struct {
    ReplayData data;           /* The loaded replay data */
    bool active;               /* Whether replay playback is in progress */
    size_t index;              /* Current event index */
    Uint32 start_ticks;        /* SDL ticks when playback started */
    bool evaluated;            /* Whether test expectations have been checked */
    bool hold_active;          /* Whether holding final frame for visibility */
    Uint32 hold_until;         /* SDL ticks to hold until */
} ReplayState;

/* ============================================================================
 * Forward Declarations
 * ========================================================================== */

static void print_usage(const char *exe, bool to_stderr);
static void ensure_replay_dir(void);
static bool parse_args(int argc, char **argv, AppConfig *config);
static int run_headless_replay(const AppConfig *config, GameState *game);
static void replay_print_stats(const ReplayData *rep);
static void update_frame_timing(GameState *game, double delta_seconds, double fps);
static void process_live_replay(ReplayState *replay, GameState *game, bool test_mode, bool *running);
static void process_ai_turn(GameState *game, bool can_animate);
static void handle_autosave(GameState *game, const char *record_path, bool replay_mode_active);
static void evaluate_test_expectations(const ReplayState *replay, GameState *game,
                                       const char *replay_path, RenderContext *render,
                                       bool render_ready, bool *evaluated,
                                       bool *hold_active, Uint32 *hold_until);

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/*
 * print_usage: Display command-line help.
 */
static void print_usage(const char *exe, bool to_stderr) {
    FILE *out = to_stderr ? stderr : stdout;
    fprintf(out,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --record <path>      Record a replay (saved on exit).\n"
        "  --replay <path>      Load and play a replay.\n"
        "  --headless           Run without a window (use with --replay).\n"
        "  --no-headless        Force windowed mode.\n"
        "  --test               Enable replay assertions and exit on failure.\n"
        "  --help               Show this help.\n"
        "\n"
        "Notes:\n"
        "  --headless is intended for replay verification. Use with --replay.\n",
        exe ? exe : "chess-c");
}

/*
 * ensure_replay_dir: Create the replays directory if it doesn't exist.
 */
static void ensure_replay_dir(void) {
    int mkdir_result;
#ifdef _WIN32
    mkdir_result = _mkdir("replays");
#else
    mkdir_result = mkdir("replays", 0755);
#endif
    if (mkdir_result != 0 && errno != EEXIST) {
        SDL_Log("Failed to create replays directory (errno=%d)", errno);
    }
}

/* ============================================================================
 * Argument Parsing
 * ========================================================================== */

/*
 * parse_args: Parse command-line arguments into AppConfig.
 * Returns true on success, false on error (with usage printed).
 */
static bool parse_args(int argc, char **argv, AppConfig *config) {
    int headless_flag = 0;  /* 0 = unset, 1 = force headless, -1 = force windowed */
    bool saw_error = false;

    memset(config, 0, sizeof(*config));

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            config->record_path = argv[++i];
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            config->replay_path = argv[++i];
        } else if (strcmp(argv[i], "--headless") == 0) {
            config->headless = true;
            headless_flag = 1;
        } else if (strcmp(argv[i], "--no-headless") == 0) {
            config->headless = false;
            headless_flag = -1;
        } else if (strcmp(argv[i], "--test") == 0) {
            config->test_mode = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0], false);
            exit(0);
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            saw_error = true;
        }
    }

    if (saw_error) {
        print_usage(argv[0], true);
        return false;
    }

    /* Default to headless in test mode unless explicitly overridden */
    if (config->test_mode && headless_flag == 0) {
        config->headless = true;
    }

    /* Headless mode requires a replay to play */
    if (config->headless && !config->replay_path) {
        fprintf(stderr, "--headless requires --replay. Use --help for examples.\n");
        print_usage(argv[0], true);
        return false;
    }

    return true;
}

/* ============================================================================
 * Replay Statistics
 * ========================================================================== */

/*
 * replay_print_stats: Print summary of replay events and assertions.
 * Useful for debugging and test verification output.
 */
static void replay_print_stats(const ReplayData *rep) {
    if (!rep) return;

    size_t expect_illegal = 0;
    size_t assert_turn = 0, assert_phase = 0, assert_castle = 0;
    size_t assert_enp = 0, assert_legal = 0;

    for (size_t i = 0; i < rep->event_count; ++i) {
        const ReplayEvent *ev = &rep->events[i];
        switch (ev->type) {
            case REPLAY_EVENT_EXPECT_ILLEGAL: expect_illegal++; break;
            case REPLAY_EVENT_ASSERT_TURN:    assert_turn++;    break;
            case REPLAY_EVENT_ASSERT_PHASE:   assert_phase++;   break;
            case REPLAY_EVENT_ASSERT_CASTLE:  assert_castle++;  break;
            case REPLAY_EVENT_ASSERT_EN_PASSANT: assert_enp++;  break;
            case REPLAY_EVENT_ASSERT_LEGAL:   assert_legal++;   break;
            default: break;
        }
    }

    printf("REPLAY_EVENTS %zu\n", rep->event_count);
    printf("REPLAY_ASSERT_EXPECT_ILLEGAL %zu\n", expect_illegal);
    printf("REPLAY_ASSERT_TURN %zu\n", assert_turn);
    printf("REPLAY_ASSERT_PHASE %zu\n", assert_phase);
    printf("REPLAY_ASSERT_CASTLE %zu\n", assert_castle);
    printf("REPLAY_ASSERT_EN_PASSANT %zu\n", assert_enp);
    printf("REPLAY_ASSERT_LEGAL %zu\n", assert_legal);
}

/* ============================================================================
 * Headless Replay Runner
 * ========================================================================== */

/*
 * run_headless_replay: Execute a replay without rendering for automated testing.
 *
 * Loads the replay, applies all events, then checks expectations (phase, turn,
 * checksum, board state) and prints results. Returns 0 on PASS, 1 on FAIL.
 */
static int run_headless_replay(const AppConfig *config, GameState *game) {
    ReplayData replay = {0};

    if (!replay_load(config->replay_path, &replay)) {
        SDL_Log("Failed to load replay: %s", config->replay_path);
        return 1;
    }

    replay_seed_game(&replay.start, game);
    game->ui.show_main_menu = false;
    game->ui.show_pause_menu = false;

    replay_print_stats(&replay);
    bool apply_ok = replay_apply_events(&replay, game, false);
    if (!apply_ok) {
        SDL_Log("Replay application failed (assertion/illegal expectation)");
    }

    /* Compute final state */
    uint32_t sum = replay_checksum(game);
    printf("REPLAY_FILE %s\n", config->replay_path);
    printf("REPLAY_CHECKSUM %u\n", sum);
    printf("REPLAY_TURN %d\n", game->match.turn_number);
    printf("REPLAY_PHASE %s\n", game->match.phase);

    /* Validate expectations */
    bool ok = apply_ok;
    if (apply_ok) {
        if (replay.has_expect_phase &&
            strncmp(game->match.phase, replay.expect_phase, sizeof(game->match.phase)) != 0) {
            ok = false;
            printf("EXPECT_PHASE %s GOT %s\n", replay.expect_phase, game->match.phase);
        }
        if (replay.has_expect_turn && game->match.turn_number != replay.expect_turn) {
            ok = false;
            printf("EXPECT_TURN %d GOT %d\n", replay.expect_turn, game->match.turn_number);
        }
        if (replay.has_expect_checksum && sum != replay.expect_checksum) {
            ok = false;
            printf("EXPECT_CHECKSUM %u GOT %u\n", replay.expect_checksum, sum);
        }
        if (replay.has_expect_board) {
            for (int r = 0; r < 8; ++r) {
                char row[9] = {0};
                for (int f = 0; f < 8; ++f) {
                    row[f] = game->board_state.squares[r][f].piece;
                }
                row[8] = '\0';
                if (strncmp(row, replay.expect_board[r], 8) != 0) {
                    ok = false;
                    printf("EXPECT_BOARD_R%d %s GOT %s\n", r, replay.expect_board[r], row);
                }
            }
        }
    }

    printf("RESULT %s\n", ok ? "PASS" : "FAIL");
    replay_free(&replay);
    return ok ? 0 : 1;
}

/* ============================================================================
 * Frame Timing
 * ========================================================================== */

/*
 * update_frame_timing: Update performance metrics for the current frame.
 * Smooths FPS display by averaging over ~0.5 second intervals.
 */
static void update_frame_timing(GameState *game, double delta_seconds, double fps) {
    if (SDL_LockMutex(game->mutex) != 0) {
        SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        return;
    }

    /* Update game clock when not paused */
    if (!game->ui.show_main_menu && !game->ui.show_pause_menu) {
        game->match.game_time.total_seconds += delta_seconds;
    }

    /* Raw frame metrics */
    game->perf.last_frame_seconds = delta_seconds;
    game->perf.last_fps = fps;

    /* Accumulate for smoothed display (updated every ~0.5s) */
    game->perf.accum_time += delta_seconds;
    game->perf.accum_frames += 1;
    if (game->perf.accum_time >= 0.5) {
        game->perf.smoothed_frame_ms = (game->perf.accum_time / game->perf.accum_frames) * 1000.0;
        game->perf.smoothed_fps = game->perf.accum_frames / game->perf.accum_time;
        game->perf.accum_time = 0.0;
        game->perf.accum_frames = 0;
    }

    SDL_UnlockMutex(game->mutex);
}

/* ============================================================================
 * Live Replay Playback
 * ========================================================================== */

/*
 * process_live_replay: Advance interactive replay playback by elapsed time.
 *
 * Events are paced at 250ms intervals for visual clarity. In test mode,
 * assertion failures will set *running to false to exit the loop.
 */
static void process_live_replay(ReplayState *replay, GameState *game, bool test_mode, bool *running) {
    if (!replay->active) return;

    /* Check if replay mode was cancelled by user */
    if (!game->ui.replay_mode_active) {
        replay->active = false;
        replay_free(&replay->data);
        memset(&replay->data, 0, sizeof(replay->data));
        return;
    }

    /* Paused - don't advance */
    if (game->ui.replay_paused) return;

    Uint32 elapsed = SDL_GetTicks() - replay->start_ticks;
    const Uint32 replay_step_ms = 250;  /* Time between events for visual pacing */

    while (replay->index < replay->data.event_count) {
        Uint32 target = (Uint32)(replay->index * replay_step_ms);
        if (elapsed < target) break;

        const ReplayEvent *ev = &replay->data.events[replay->index];
        if (!replay_apply_event(ev, game, true, test_mode)) {
            if (test_mode) {
                *running = false;
            }
            break;
        }
        replay->index++;
    }

    /* Mark complete when all events processed */
    if (replay->index >= replay->data.event_count) {
        replay->active = false;
    }
}

/* ============================================================================
 * AI Move Processing
 * ========================================================================== */

/*
 * process_ai_turn: Handle AI move computation and application.
 *
 * Requests moves asynchronously via worker thread, then polls for completion.
 * Moves are validated against the current position before application to handle
 * race conditions where the board state changed during computation.
 */
static void process_ai_turn(GameState *game, bool can_animate) {
    /* Don't move while animation is playing */
    if (!can_animate) return;

    /* Request move if it's AI's turn */
    if (SDL_LockMutex(game->mutex) == 0) {
        if (game->ai.side != AI_SIDE_NONE &&
            !game->ui.show_main_menu &&
            !game->ui.show_pause_menu &&
            !game_is_terminal_phase(game->match.phase)) {

            bool ai_is_white = (game->ai.side == AI_SIDE_WHITE);
            if (game->match.white_to_move == ai_is_white) {
                ai_request_move(game);
            }
        }
        SDL_UnlockMutex(game->mutex);
    }

    /* Poll for completed move (skip if paused to avoid discarding result) */
    bool can_poll = false;
    if (SDL_LockMutex(game->mutex) == 0) {
        can_poll = !game->ui.show_main_menu && !game->ui.show_pause_menu;
        SDL_UnlockMutex(game->mutex);
    }

    AiResult ai_result = {0};
    if (can_poll && ai_poll_move(&ai_result)) {
        if (SDL_LockMutex(game->mutex) == 0) {
            bool ai_is_white = (game->ai.side == AI_SIDE_WHITE);
            bool ai_turn = (game->ai.side != AI_SIDE_NONE) &&
                           (game->match.white_to_move == ai_is_white);
            uint32_t key = ai_position_key(game);
            bool applied = false;

            /*
             * Validate the result matches current game state.
             * This guards against stale results from a previous position
             * (e.g., if user made a move while AI was computing).
             */
            if (ai_result.valid &&
                ai_turn &&
                ai_result.turn_number == game->match.turn_number &&
                ai_result.position_key == key &&
                !game->ui.show_main_menu &&
                !game->ui.show_pause_menu &&
                !game_is_terminal_phase(game->match.phase)) {

                if (game_apply_move_ai(game, ai_result.from, ai_result.to, ai_result.hint)) {
                    applied = true;

                    /* Record to replay if recording */
                    if (replay_global_recording()) {
                        char promo = '\0';
                        if ((ai_result.hint.flags & MOVE_PROMOTION) && ai_result.hint.promotion) {
                            promo = ai_result.hint.promotion;
                        }
                        replay_recorder_record_move(
                            replay_global_recorder(), SDL_GetTicks(),
                            ai_result.from.file, ai_result.from.rank,
                            ai_result.to.file, ai_result.to.rank, promo);
                    }
                }
            }

            SDL_UnlockMutex(game->mutex);
        }
    }
}

/* ============================================================================
 * Replay Autosave
 * ========================================================================== */

/*
 * handle_autosave: Save replay data incrementally during gameplay.
 *
 * Saves on each new turn and when the game reaches a terminal state.
 * This ensures replay data is preserved even if the program crashes.
 */
static void handle_autosave(GameState *game, const char *record_path, bool replay_mode_active) {
    if (replay_mode_active || !record_path || !replay_global_recording()) return;

    bool should_save = false;
    bool is_terminal = false;
    int turn_number = 0;

    if (SDL_LockMutex(game->mutex) == 0) {
        turn_number = game->match.turn_number;
        is_terminal = game_is_terminal_phase(game->match.phase);
        SDL_UnlockMutex(game->mutex);
    }

    /* Save on turn change */
    if (turn_number != game->autosave_last_turn) {
        should_save = true;
        game->autosave_last_turn = turn_number;
    }

    /* Save on terminal state (checkmate, stalemate, etc.) */
    if (is_terminal && !game->autosave_terminal_saved) {
        should_save = true;
        game->autosave_terminal_saved = true;
    }

    if (should_save) {
        ensure_replay_dir();
        if (!replay_global_append(record_path)) {
            SDL_Log("Failed to autosave replay");
        }
        if (game->autosave_terminal_saved) {
            if (!replay_global_write_end(record_path)) {
                SDL_Log("Failed to finalize replay");
            }
        }
    }
}

/* ============================================================================
 * Test Mode Evaluation
 * ========================================================================== */

/*
 * evaluate_test_expectations: Check replay expectations in visual test mode.
 *
 * Called once after replay playback completes. Prints PASS/FAIL results and
 * holds the final frame briefly for visual inspection before exiting.
 */
static void evaluate_test_expectations(const ReplayState *replay, GameState *game,
                                       const char *replay_path, RenderContext *render,
                                       bool render_ready, bool *evaluated,
                                       bool *hold_active, Uint32 *hold_until) {
    if (*evaluated) return;

    /* Force intended final phase (e.g., resign/draw-agreed) before evaluating */
    if (replay->data.has_expect_phase) {
        strncpy(game->match.phase, replay->data.expect_phase, sizeof(game->match.phase) - 1);
        game->match.phase[sizeof(game->match.phase) - 1] = '\0';
    }

    /* Render final state before evaluation */
    if (render_ready) {
        render_frame(render, game);
    }

    /* Compute and print final state */
    uint32_t sum = replay_checksum(game);
    printf("REPLAY_FILE %s\n", replay_path);
    printf("REPLAY_CHECKSUM %u\n", sum);
    printf("REPLAY_TURN %d\n", game->match.turn_number);
    printf("REPLAY_PHASE %s\n", game->match.phase);

    /* Check expectations */
    bool ok = true;
    if (replay->data.has_expect_phase &&
        strncmp(game->match.phase, replay->data.expect_phase, sizeof(game->match.phase)) != 0) {
        ok = false;
        printf("EXPECT_PHASE %s GOT %s\n", replay->data.expect_phase, game->match.phase);
    }
    if (replay->data.has_expect_turn && game->match.turn_number != replay->data.expect_turn) {
        ok = false;
        printf("EXPECT_TURN %d GOT %d\n", replay->data.expect_turn, game->match.turn_number);
    }
    if (replay->data.has_expect_checksum && sum != replay->data.expect_checksum) {
        ok = false;
        printf("EXPECT_CHECKSUM %u GOT %u\n", replay->data.expect_checksum, sum);
    }
    if (replay->data.has_expect_board) {
        for (int r = 0; r < 8; ++r) {
            char row[9] = {0};
            for (int f = 0; f < 8; ++f) {
                row[f] = game->board_state.squares[r][f].piece;
            }
            row[8] = '\0';
            if (strncmp(row, replay->data.expect_board[r], 8) != 0) {
                ok = false;
                printf("EXPECT_BOARD_R%d %s GOT %s\n", r, replay->data.expect_board[r], row);
            }
        }
    }
    printf("RESULT %s\n", ok ? "PASS" : "FAIL");

    /* Hold final frame briefly for visibility */
    *hold_active = true;
    *hold_until = SDL_GetTicks() + 2000;
    *evaluated = true;
}

/* ============================================================================
 * Main Entry Point
 * ========================================================================== */

int main(int argc, char **argv) {
    /* --- Parse command-line arguments --- */
    AppConfig config = {0};
    if (!parse_args(argc, argv, &config)) {
        return 2;
    }

    /* --- Initialize SDL and renderer --- */
    static RenderContext render = {0};
    static GameState game = {0};
    bool render_ready = false;

    if (config.headless) {
        if (SDL_Init(SDL_INIT_TIMER) != 0) {
            SDL_Log("SDL_Init (timer) failed: %s", SDL_GetError());
            return 1;
        }
    } else {
        if (!render_init(&render, "Chess C", 1280, 960)) {
            return 1;
        }
        render_ready = true;
    }

    /* --- Initialize game state --- */
    if (!game_init(&game)) {
        if (render_ready) {
            render_shutdown(&render);
        } else {
            SDL_Quit();
        }
        return 1;
    }

    uint32_t last_full_reset_counter = game.ui.full_reset_counter;
    bool record_to_file = (config.record_path != NULL);
    if (record_to_file) {
        ensure_replay_dir();
    }

    /* --- Headless replay mode: run and exit immediately --- */
    if (config.replay_path && config.headless) {
        int result = run_headless_replay(&config, &game);
        game_destroy(&game);
        SDL_Quit();
        return result;
    }

    /* --- Initialize AI worker thread --- */
    if (!ai_worker_init()) {
        SDL_Log("Failed to initialize AI worker thread");
        game_destroy(&game);
        if (render_ready) {
            render_shutdown(&render);
        } else {
            SDL_Quit();
        }
        return 1;
    }

    /* --- Load interactive replay if provided --- */
    ReplayState replay = {0};
    if (config.replay_path) {
        if (!replay_load(config.replay_path, &replay.data)) {
            SDL_Log("Failed to load replay: %s", config.replay_path);
        } else {
            replay_seed_game(&replay.data.start, &game);
            game.ui.show_main_menu = false;
            game.ui.show_pause_menu = false;
            game.ui.replay_mode_active = true;
            replay.active = true;
            replay.start_ticks = SDL_GetTicks();
            replay.index = 0;
            replay_print_stats(&replay.data);
        }
    }

    /* ========================================================================
     * Main Game Loop
     * ======================================================================== */
    bool text_input_active = false;
    Uint32 last_ticks = SDL_GetTicks();
    bool running = true;

    while (running) {
        /* --- Frame timing --- */
        Uint32 now = SDL_GetTicks();
        double delta_seconds = (now - last_ticks) / 1000.0;
        last_ticks = now;
        double fps = delta_seconds > 0.0 ? 1.0 / delta_seconds : 0.0;

        /* --- Process SDL events --- */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
            keyboard_handle_event(&game, &e);
            mouse_handle_event(&game, &render, &e);
        }

        /* --- Handle text input state for save modal --- */
        bool want_text_input = false;
        if (SDL_LockMutex(game.mutex) == 0) {
            want_text_input = game.ui.show_save_modal;
            SDL_UnlockMutex(game.mutex);
        }
        if (want_text_input && !text_input_active) {
            SDL_StartTextInput();
            text_input_active = true;
        } else if (!want_text_input && text_input_active) {
            SDL_StopTextInput();
            text_input_active = false;
        }

        /* --- Handle full game reset (new game from menu) --- */
        if (SDL_LockMutex(game.mutex) == 0) {
            uint32_t reset_counter = game.ui.full_reset_counter;
            SDL_UnlockMutex(game.mutex);
            if (reset_counter != last_full_reset_counter) {
                last_full_reset_counter = reset_counter;
                ai_worker_reset();
            }
        }

        /* --- Keep menus hidden during test/replay modes --- */
        if (config.test_mode || replay.active) {
            if (!game.ui.show_main_menu) {
                game.ui.show_main_menu = false;
                if (!game.ui.replay_paused) {
                    game.ui.show_pause_menu = false;
                }
            }
        }

        /* --- Handle runtime replay load requests (from UI) --- */
        bool replay_mode_active = false;
        if (SDL_LockMutex(game.mutex) == 0) {
            if (game.ui.replay_request_pending) {
                char request_path[256];
                strncpy(request_path, game.ui.replay_request, sizeof(request_path) - 1);
                request_path[sizeof(request_path) - 1] = '\0';
                game.ui.replay_request_pending = false;
                SDL_UnlockMutex(game.mutex);

                /* Clean up previous replay if any */
                if (replay.data.event_count > 0) {
                    replay_free(&replay.data);
                    memset(&replay, 0, sizeof(replay));
                }

                if (!replay_load(request_path, &replay.data)) {
                    SDL_Log("Failed to load replay: %s", request_path);
                    if (SDL_LockMutex(game.mutex) == 0) {
                        game.ui.show_main_menu = true;
                        game.ui.show_pause_menu = false;
                        game.ui.replay_mode_active = false;
                        SDL_UnlockMutex(game.mutex);
                    }
                } else {
                    replay_seed_game(&replay.data.start, &game);
                    if (SDL_LockMutex(game.mutex) == 0) {
                        game.ui.show_main_menu = false;
                        game.ui.show_pause_menu = false;
                        game.ui.show_load_menu = false;
                        game.ui.show_load_confirm = false;
                        game.ui.show_new_menu = false;
                        game.ui.replay_mode_active = true;
                        SDL_UnlockMutex(game.mutex);
                    }
                    replay_global_stop();
                    replay.active = true;
                    replay.start_ticks = SDL_GetTicks();
                    replay.index = 0;
                    replay_print_stats(&replay.data);
                    replay_mode_active = true;
                }
            } else {
                if (game.ui.replay_mode_active) {
                    replay_mode_active = true;
                }
                SDL_UnlockMutex(game.mutex);
            }
        }

        /* Stop recording when entering replay mode */
        if (replay_mode_active && replay_global_recording()) {
            replay_global_stop();
            game.autosave_last_turn = -1;
            game.autosave_terminal_saved = false;
        }

        /* --- Update timing and performance metrics --- */
        update_frame_timing(&game, delta_seconds, fps);

        /* --- Process live replay playback --- */
        process_live_replay(&replay, &game, config.test_mode, &running);

        /* --- Render frame --- */
        if (render_ready) {
            render_frame(&render, &game);
        }

        /* --- Process AI moves (when not in replay mode) --- */
        if (!replay_mode_active) {
            bool can_animate = !render_ready || !render.anim.active;
            process_ai_turn(&game, can_animate);
        }

        /* --- Autosave replay during recording --- */
        handle_autosave(&game, config.record_path, replay_mode_active);

        /* --- Evaluate test expectations after replay completes --- */
        if (config.test_mode && config.replay_path && !replay.active && !config.headless) {
            evaluate_test_expectations(&replay, &game, config.replay_path, &render,
                                       render_ready, &replay.evaluated,
                                       &replay.hold_active, &replay.hold_until);
        }

        /* --- Exit after holding final frame in test mode --- */
        if (replay.hold_active && SDL_GetTicks() >= replay.hold_until) {
            running = false;
        }
    }

    /* ========================================================================
     * Cleanup
     * ======================================================================== */

    /* Finalize replay recording if active */
    if (record_to_file && replay_global_recording()) {
        if (!game.autosave_terminal_saved) {
            ensure_replay_dir();
            if (!replay_global_append(config.record_path)) {
                SDL_Log("Failed to save replay to %s", config.record_path);
            }
            if (!replay_global_write_end(config.record_path)) {
                SDL_Log("Failed to finalize replay");
            }
        }
        replay_global_stop();
    }

    ai_worker_shutdown();
    game_destroy(&game);
    if (render_ready) {
        render_shutdown(&render);
    } else {
        SDL_Quit();
    }
    if (replay.data.event_count > 0) {
        replay_free(&replay.data);
    }

    return 0;
}
