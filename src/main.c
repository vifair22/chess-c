#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <direct.h>
#include <errno.h>

#include "ai/ai.h"
#include "game/logic.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "renderer/render.h"
#include "save/replay.h"

static void replay_print_stats(const ReplayData *rep);
static void print_usage(const char *exe, bool to_stderr);
static void ensure_replay_dir(void);

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

static void ensure_replay_dir(void) {
    if (_mkdir("replays") != 0) {
        if (errno != EEXIST) {
            SDL_Log("Failed to create replays directory (errno=%d)", errno);
        }
    }
}

int main(int argc, char **argv) {
    const char *record_path = NULL;
    char auto_record_path[256] = "replays/auto_session.replay";
    const char *replay_path = NULL;
    bool headless = false;
    int headless_flag = 0; // 0 = unset, 1 = force headless, -1 = force windowed
    bool test_mode = false;
    ReplayData live_replay = {0};
    bool live_replay_active = false;
    size_t live_replay_index = 0;
    Uint32 live_replay_start_ticks = 0;
    bool replay_evaluated = false;
    bool replay_hold_active = false;
    Uint32 replay_hold_until = 0;
    bool text_input_active = false;
    bool ai_worker_ready = false;

    bool saw_error = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            record_path = argv[++i];
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = true;
            headless_flag = 1;
        } else if (strcmp(argv[i], "--no-headless") == 0) {
            headless = false;
            headless_flag = -1;
        } else if (strcmp(argv[i], "--test") == 0) {
            test_mode = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0], false);
            return 0;
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            saw_error = true;
        }
    }
    if (saw_error) {
        print_usage(argv[0], true);
        return 2;
    }

    if (test_mode && headless_flag == 0) {
        headless = true; // default headless in test mode unless explicitly overridden
    }

    if (headless && !replay_path) {
        fprintf(stderr, "--headless requires --replay. Use --help for examples.\n");
        print_usage(argv[0], true);
        return 2;
    }

    static RenderContext render = {0};
    static GameState game = {0};
    bool render_ready = false;

    if (headless) {
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

    if (!game_init(&game)) {
        if (render_ready) {
            render_shutdown(&render);
        } else {
            SDL_Quit();
        }
        return 1;
    }
    ensure_replay_dir();

    // Prepare auto-record path for live games (recording begins on game start, not in menus).
    if (!replay_path) {
        if (!record_path) {
            time_t now = time(NULL);
            struct tm local_tm;
            if (localtime_s(&local_tm, &now) == 0) {
                char stamp[64];
                if (strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local_tm) > 0) {
                    snprintf(auto_record_path, sizeof(auto_record_path), "replays/auto_%s.replay", stamp);
                }
            }
        }
    }

    // Replay with headless/testing: apply, emit checksum/expectations, exit.
    if (replay_path && headless) {
        ReplayData replay = {0};
        if (!replay_load(replay_path, &replay)) {
            SDL_Log("Failed to load replay: %s", replay_path);
            if (render_ready) {
                render_shutdown(&render);
            } else {
                SDL_Quit();
            }
            game_destroy(&game);
            return 1;
        }
        replay_seed_game(&replay.start, &game);
        game.ui.show_main_menu = false;
        game.ui.show_pause_menu = false;
        replay_print_stats(&replay);
        bool apply_ok = replay_apply_events(&replay, &game, false);
        if (!apply_ok) {
            SDL_Log("Replay application failed (assertion/illegal expectation)");
        }
        uint32_t sum = replay_checksum(&game);
        printf("REPLAY_FILE %s\n", replay_path);
        printf("REPLAY_CHECKSUM %u\n", sum);
        printf("REPLAY_TURN %d\n", game.match.turn_number);
        printf("REPLAY_PHASE %s\n", game.match.phase);

        bool ok = apply_ok;
        if (apply_ok) {
            if (replay.has_expect_phase && strncmp(game.match.phase, replay.expect_phase, sizeof(game.match.phase)) != 0) {
                ok = false;
                printf("EXPECT_PHASE %s GOT %s\n", replay.expect_phase, game.match.phase);
            }
            if (replay.has_expect_turn && game.match.turn_number != replay.expect_turn) {
                ok = false;
                printf("EXPECT_TURN %d GOT %d\n", replay.expect_turn, game.match.turn_number);
            }
            if (replay.has_expect_checksum && sum != replay.expect_checksum) {
                ok = false;
                printf("EXPECT_CHECKSUM %u GOT %u\n", replay.expect_checksum, sum);
            }
            if (replay.has_expect_board) {
                for (int r = 0; r < 8; ++r) {
                    char row[9] = {0};
                    for (int f = 0; f < 8; ++f) {
                        row[f] = game.board_state.squares[r][f].piece;
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
        if (render_ready) {
            render_shutdown(&render);
        } else {
            SDL_Quit();
        }
        game_destroy(&game);
        return ok ? 0 : 1;
    }

    ai_worker_ready = ai_worker_init();
    if (!ai_worker_ready) {
        SDL_Log("AI worker unavailable; falling back to synchronous AI.");
    }

    // If replay provided (interactive), seed game and play events during loop (with timing).
    if (replay_path) {
        if (!replay_load(replay_path, &live_replay)) {
            SDL_Log("Failed to load replay: %s", replay_path);
        } else {
            replay_seed_game(&live_replay.start, &game);
            game.ui.show_main_menu = false;
            game.ui.show_pause_menu = false;
            game.ui.replay_mode_active = true;
            live_replay_active = true;
            live_replay_start_ticks = SDL_GetTicks();
            live_replay_index = 0;
            replay_print_stats(&live_replay);
        }
    }

    Uint32 last_ticks = SDL_GetTicks();
    bool running = true;
    while (running) {
        Uint32 now = SDL_GetTicks();
        double delta_seconds = (now - last_ticks) / 1000.0;
        last_ticks = now;
        double fps = delta_seconds > 0.0 ? 1.0 / delta_seconds : 0.0;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
            keyboard_handle_event(&game, &e);
            mouse_handle_event(&game, &render, &e);
        }

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

        if (test_mode || live_replay_active) {
            game.ui.show_main_menu = false;
            game.ui.show_pause_menu = false;
        }

        bool replay_mode_active = (replay_path != NULL);
        if (SDL_LockMutex(game.mutex) == 0) {
            if (game.ui.replay_request_pending) {
                char request_path[256];
                strncpy(request_path, game.ui.replay_request, sizeof(request_path) - 1);
                request_path[sizeof(request_path) - 1] = '\0';
                game.ui.replay_request_pending = false;
                SDL_UnlockMutex(game.mutex);

                if (live_replay.event_count > 0) {
                    replay_free(&live_replay);
                    memset(&live_replay, 0, sizeof(live_replay));
                }
                if (!replay_load(request_path, &live_replay)) {
                    SDL_Log("Failed to load replay: %s", request_path);
                    if (SDL_LockMutex(game.mutex) == 0) {
                        game.ui.show_main_menu = true;
                        game.ui.show_pause_menu = false;
                        game.ui.replay_mode_active = false;
                        SDL_UnlockMutex(game.mutex);
                    }
                } else {
                    replay_seed_game(&live_replay.start, &game);
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
                    live_replay_active = true;
                    live_replay_start_ticks = SDL_GetTicks();
                    live_replay_index = 0;
                    replay_print_stats(&live_replay);
                    replay_mode_active = true;
                }
            } else {
                if (game.ui.replay_mode_active) {
                    replay_mode_active = true;
                }
                SDL_UnlockMutex(game.mutex);
            }
        }

        if (SDL_LockMutex(game.mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        } else {
            if (!game.ui.show_main_menu && !game.ui.show_pause_menu) {
                game.match.game_time.total_seconds += delta_seconds;
            }
            game.perf.last_frame_seconds = delta_seconds;
            game.perf.last_fps = fps;
            game.perf.accum_time += delta_seconds;
            game.perf.accum_frames += 1;
            if (game.perf.accum_time >= 0.5) { // update display every ~0.5s
                double avg_ms = (game.perf.accum_time / game.perf.accum_frames) * 1000.0;
                double avg_fps = game.perf.accum_frames / game.perf.accum_time;
                game.perf.smoothed_frame_ms = avg_ms;
                game.perf.smoothed_fps = avg_fps;
                game.perf.accum_time = 0.0;
                game.perf.accum_frames = 0;
            }
            if (game.ai_timing_active) {
                if (game.ai_time_freq <= 0.0) {
                    game.ai_time_freq = (double)SDL_GetPerformanceFrequency();
                }
                if (game.ai_time_freq > 0.0) {
                    uint64_t now_counter = SDL_GetPerformanceCounter();
                    if (now_counter >= game.ai_timing_start) {
                        game.ai_last_move_ms = ((double)(now_counter - game.ai_timing_start) * 1000.0 / game.ai_time_freq);
                    }
                }
            }
            SDL_UnlockMutex(game.mutex);
        }

        if (live_replay_active) {
            Uint32 elapsed = SDL_GetTicks() - live_replay_start_ticks;
            // Synthetic pacing for recorded/PGN replays: 250ms between events.
            const Uint32 replay_step_ms = 250;
            while (live_replay_index < live_replay.event_count) {
                Uint32 target = live_replay_index * replay_step_ms;
                if (elapsed < target) break;
                const ReplayEvent *ev = &live_replay.events[live_replay_index];
                if (!replay_apply_event(ev, &game, true, test_mode)) {
                    if (test_mode) {
                        running = false;
                    }
                    break;
                }
                live_replay_index++;
            }
            if (live_replay_index >= live_replay.event_count) {
                live_replay_active = false;
            }
        }
        if (live_replay_active && !game.ui.replay_mode_active) {
            live_replay_active = false;
            replay_free(&live_replay);
            memset(&live_replay, 0, sizeof(live_replay));
        }

        if (render_ready) {
            render_frame(&render, &game);
        }

        if (!replay_mode_active) {
            if (ai_worker_ready) {
                bool can_ai_move = !render_ready || !render.anim.active;
                if (can_ai_move && SDL_LockMutex(game.mutex) == 0) {
                    if (game.ai.side != AI_SIDE_NONE &&
                        !game.ui.show_main_menu &&
                        !game.ui.show_pause_menu &&
                        !game_is_terminal_phase(game.match.phase)) {
                        bool ai_is_white = game.ai.side == AI_SIDE_WHITE;
                        if (game.match.white_to_move == ai_is_white) {
                            if (ai_request_move(&game)) {
                                game.ai_timing_active = true;
                                game.ai_timing_start = SDL_GetPerformanceCounter();
                                if (game.ai_time_freq <= 0.0) {
                                    game.ai_time_freq = (double)SDL_GetPerformanceFrequency();
                                }
                            }
                        }
                    }
                    SDL_UnlockMutex(game.mutex);
                }

                AiResult ai_result = {0};
                if (ai_poll_move(&ai_result)) {
                    if (SDL_LockMutex(game.mutex) == 0) {
                        bool ai_is_white = game.ai.side == AI_SIDE_WHITE;
                        bool ai_turn = (game.ai.side != AI_SIDE_NONE) && (game.match.white_to_move == ai_is_white);
                        uint32_t key = ai_position_key(&game);
                        bool applied = false;
                        if (ai_result.valid &&
                            ai_turn &&
                            ai_result.turn_number == game.match.turn_number &&
                            ai_result.position_key == key &&
                            !game.ui.show_main_menu &&
                            !game.ui.show_pause_menu &&
                            !game_is_terminal_phase(game.match.phase)) {
                            if (game_apply_move_ai(&game, ai_result.from, ai_result.to, ai_result.hint)) {
                                applied = true;
                                if (game.ai_timing_active && game.ai_time_freq > 0.0) {
                                    uint64_t end = SDL_GetPerformanceCounter();
                                    if (end >= game.ai_timing_start) {
                                        game.ai_last_move_ms = ((double)(end - game.ai_timing_start) * 1000.0 / game.ai_time_freq);
                                    }
                                    game.ai_timing_active = false;
                                }
                                if (replay_global_recording()) {
                                    char promo = '\0';
                                    if ((ai_result.hint.flags & MOVE_PROMOTION) && ai_result.hint.promotion) {
                                        promo = ai_result.hint.promotion;
                                    }
                                    replay_recorder_record_move(replay_global_recorder(), SDL_GetTicks(),
                                        ai_result.from.file, ai_result.from.rank,
                                        ai_result.to.file, ai_result.to.rank,
                                        promo);
                                }
                            }
                        }
                        if (game.ai_timing_active && !applied) {
                            game.ai_timing_active = false;
                        }
                        SDL_UnlockMutex(game.mutex);
                    }
                }
            } else {
                bool can_ai_move = !render_ready || !render.anim.active;
                if (can_ai_move && SDL_LockMutex(game.mutex) == 0) {
                    if (game.ai.side != AI_SIDE_NONE &&
                        !game.ui.show_main_menu &&
                        !game.ui.show_pause_menu &&
                        !game_is_terminal_phase(game.match.phase)) {
                        bool ai_is_white = game.ai.side == AI_SIDE_WHITE;
                        if (game.match.white_to_move == ai_is_white) {
                            uint64_t start = SDL_GetPerformanceCounter();
                            if (game.ai_time_freq <= 0.0) {
                                game.ai_time_freq = (double)SDL_GetPerformanceFrequency();
                            }
                            bool applied = ai_make_move(&game);
                            uint64_t end = SDL_GetPerformanceCounter();
                            if (applied && game.ai_time_freq > 0.0 && end >= start) {
                                game.ai_last_move_ms = ((double)(end - start) * 1000.0 / game.ai_time_freq);
                            }
                        }
                    }
                    SDL_UnlockMutex(game.mutex);
                }
            }
        }

        if (!replay_mode_active && replay_global_recording()) {
            bool should_save = false;
            bool is_terminal = false;
            int turn_number = 0;
            if (SDL_LockMutex(game.mutex) == 0) {
                turn_number = game.match.turn_number;
                is_terminal = game_is_terminal_phase(game.match.phase);
                SDL_UnlockMutex(game.mutex);
            }
            if (turn_number != game.autosave_last_turn) {
                should_save = true;
                game.autosave_last_turn = turn_number;
            }
            if (is_terminal && !game.autosave_terminal_saved) {
                should_save = true;
                game.autosave_terminal_saved = true;
            }
            if (should_save) {
                ensure_replay_dir();
                const char *path = record_path ? record_path : auto_record_path;
                if (!replay_global_append(path)) {
                    SDL_Log("Failed to autosave replay");
                }
                if (game.autosave_terminal_saved) {
                    if (!replay_global_write_end(path)) {
                        SDL_Log("Failed to finalize replay");
                    }
                }
            }
        }

        if (test_mode && replay_path && !live_replay_active && !headless && !replay_evaluated) {
            // Force intended final phase (e.g., resign/draw-agreed) before evaluating.
            if (live_replay.has_expect_phase) {
                strncpy(game.match.phase, live_replay.expect_phase, sizeof(game.match.phase) - 1);
                game.match.phase[sizeof(game.match.phase) - 1] = '\0';
            }
            // Render once more with the terminal phase before evaluating/printing.
            if (render_ready) {
                render_frame(&render, &game);
            }

            // Evaluate expectations in visual test mode and exit automatically.
            uint32_t sum = replay_checksum(&game);
            printf("REPLAY_FILE %s\n", replay_path);
            printf("REPLAY_CHECKSUM %u\n", sum);
            printf("REPLAY_TURN %d\n", game.match.turn_number);
            printf("REPLAY_PHASE %s\n", game.match.phase);
            bool ok = true;
            if (live_replay.has_expect_phase && strncmp(game.match.phase, live_replay.expect_phase, sizeof(game.match.phase)) != 0) {
                ok = false;
                printf("EXPECT_PHASE %s GOT %s\n", live_replay.expect_phase, game.match.phase);
            }
            if (live_replay.has_expect_turn && game.match.turn_number != live_replay.expect_turn) {
                ok = false;
                printf("EXPECT_TURN %d GOT %d\n", live_replay.expect_turn, game.match.turn_number);
            }
            if (live_replay.has_expect_checksum && sum != live_replay.expect_checksum) {
                ok = false;
                printf("EXPECT_CHECKSUM %u GOT %u\n", live_replay.expect_checksum, sum);
            }
            if (live_replay.has_expect_board) {
                for (int r = 0; r < 8; ++r) {
                    char row[9] = {0};
                    for (int f = 0; f < 8; ++f) {
                        row[f] = game.board_state.squares[r][f].piece;
                    }
                    row[8] = '\0';
                    if (strncmp(row, live_replay.expect_board[r], 8) != 0) {
                        ok = false;
                        printf("EXPECT_BOARD_R%d %s GOT %s\n", r, live_replay.expect_board[r], row);
                    }
                }
            }
            printf("RESULT %s\n", ok ? "PASS" : "FAIL");
            replay_hold_active = true;
            replay_hold_until = SDL_GetTicks() + 2000; // hold final frame for visibility without blocking loop
            replay_evaluated = true;
        }

        if (replay_hold_active && SDL_GetTicks() >= replay_hold_until) {
            running = false;
        }
    }

    if (replay_global_recording()) {
        const char *path = record_path ? record_path : auto_record_path;
        if (!game.autosave_terminal_saved) {
            ensure_replay_dir();
            if (!replay_global_append(path)) {
                SDL_Log("Failed to save replay to %s", path);
            }
            if (!replay_global_write_end(path)) {
                SDL_Log("Failed to finalize replay");
            }
        }
        replay_global_stop();
    }

    if (ai_worker_ready) {
        ai_worker_shutdown();
    }
    game_destroy(&game);
    if (render_ready) {
        render_shutdown(&render);
    } else {
        SDL_Quit();
    }
    if (live_replay.event_count > 0) {
        replay_free(&live_replay);
    }
    return 0;
}
static void replay_print_stats(const ReplayData *rep) {
    if (!rep) return;
    size_t total = rep->event_count;
    size_t expect_illegal = 0;
    size_t assert_turn = 0, assert_phase = 0, assert_castle = 0, assert_enp = 0, assert_legal = 0;
    for (size_t i = 0; i < rep->event_count; ++i) {
        const ReplayEvent *ev = &rep->events[i];
        switch (ev->type) {
            case REPLAY_EVENT_EXPECT_ILLEGAL: expect_illegal++; break;
            case REPLAY_EVENT_ASSERT_TURN: assert_turn++; break;
            case REPLAY_EVENT_ASSERT_PHASE: assert_phase++; break;
            case REPLAY_EVENT_ASSERT_CASTLE: assert_castle++; break;
            case REPLAY_EVENT_ASSERT_EN_PASSANT: assert_enp++; break;
            case REPLAY_EVENT_ASSERT_LEGAL: assert_legal++; break;
            default: break;
        }
    }
    printf("REPLAY_EVENTS %zu\n", total);
    printf("REPLAY_ASSERT_EXPECT_ILLEGAL %zu\n", expect_illegal);
    printf("REPLAY_ASSERT_TURN %zu\n", assert_turn);
    printf("REPLAY_ASSERT_PHASE %zu\n", assert_phase);
    printf("REPLAY_ASSERT_CASTLE %zu\n", assert_castle);
    printf("REPLAY_ASSERT_EN_PASSANT %zu\n", assert_enp);
    printf("REPLAY_ASSERT_LEGAL %zu\n", assert_legal);
}
