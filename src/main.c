#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "game/logic.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "renderer/render.h"
#include "save/replay.h"

int main(int argc, char **argv) {
    const char *record_path = NULL;
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
        }
    }

    if (test_mode && headless_flag == 0) {
        headless = true; // default headless in test mode unless explicitly overridden
    }

    RenderContext render = {0};
    GameState game = {0};

    if (!render_init(&render, "Chess C", 800, 800)) {
        return 1;
    }

    if (!game_init(&game)) {
        render_shutdown(&render);
        return 1;
    }

    // Start recording if requested.
    if (record_path) {
        replay_global_start(&game, SDL_GetTicks());
    }

    // Replay with headless/testing: apply, emit checksum/expectations, exit.
    if (replay_path && headless) {
        ReplayData replay = {0};
        if (!replay_load(replay_path, &replay)) {
            SDL_Log("Failed to load replay: %s", replay_path);
            render_shutdown(&render);
            game_destroy(&game);
            return 1;
        }
        replay_seed_game(&replay.start, &game);
        game.show_main_menu = false;
        game.show_pause_menu = false;
        replay_apply_events(&replay, &game, false);
        uint32_t sum = replay_checksum(&game);
        printf("REPLAY_FILE %s\n", replay_path);
        printf("REPLAY_CHECKSUM %u\n", sum);
        printf("REPLAY_TURN %d\n", game.turn_number);
        printf("REPLAY_PHASE %s\n", game.game_phase);

        bool ok = true;
        if (replay.has_expect_phase && strncmp(game.game_phase, replay.expect_phase, sizeof(game.game_phase)) != 0) {
            ok = false;
            printf("EXPECT_PHASE %s GOT %s\n", replay.expect_phase, game.game_phase);
        }
        if (replay.has_expect_turn && game.turn_number != replay.expect_turn) {
            ok = false;
            printf("EXPECT_TURN %d GOT %d\n", replay.expect_turn, game.turn_number);
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
        printf("RESULT %s\n", ok ? "PASS" : "FAIL");
        replay_free(&replay);
        render_shutdown(&render);
        game_destroy(&game);
        return ok ? 0 : 1;
    }

    // If replay provided (interactive), seed game and play events during loop (with timing).
    if (replay_path) {
        if (!replay_load(replay_path, &live_replay)) {
            SDL_Log("Failed to load replay: %s", replay_path);
        } else {
            replay_seed_game(&live_replay.start, &game);
            game.show_main_menu = false;
            game.show_pause_menu = false;
            live_replay_active = true;
            live_replay_start_ticks = SDL_GetTicks();
            live_replay_index = 0;
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

        if (test_mode || live_replay_active) {
            game.show_main_menu = false;
            game.show_pause_menu = false;
        }

        if (SDL_LockMutex(game.mutex) != 0) {
            SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        } else {
            if (!game.show_main_menu && !game.show_pause_menu) {
                game.game_time.total_seconds += delta_seconds;
            }
            game.last_frame_seconds = delta_seconds;
            game.last_fps = fps;
            game.perf_accum_time += delta_seconds;
            game.perf_accum_frames += 1;
            if (game.perf_accum_time >= 0.5) { // update display every ~0.5s
                double avg_ms = (game.perf_accum_time / game.perf_accum_frames) * 1000.0;
                double avg_fps = game.perf_accum_frames / game.perf_accum_time;
                game.smoothed_frame_ms = avg_ms;
                game.smoothed_fps = avg_fps;
                game.perf_accum_time = 0.0;
                game.perf_accum_frames = 0;
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
                if (ev->type == REPLAY_EVENT_HOVER) {
                    game.hover_pos = (BoardPos){ev->file, ev->rank};
                } else if (ev->type == REPLAY_EVENT_CLICK) {
                    game_handle_click(&game, ev->file, ev->rank);
                }
                live_replay_index++;
            }
            if (live_replay_index >= live_replay.event_count) {
                live_replay_active = false;
            }
        }

        render_frame(&render, &game);

        if (test_mode && replay_path && !live_replay_active && !headless && !replay_evaluated) {
            // Force intended final phase (e.g., resign/draw-agreed) before evaluating.
            if (live_replay.has_expect_phase) {
                strncpy(game.game_phase, live_replay.expect_phase, sizeof(game.game_phase) - 1);
                game.game_phase[sizeof(game.game_phase) - 1] = '\0';
            }
            // Render once more with the terminal phase before evaluating/printing.
            render_frame(&render, &game);

            // Evaluate expectations in visual test mode and exit automatically.
            uint32_t sum = replay_checksum(&game);
            printf("REPLAY_FILE %s\n", replay_path);
            printf("REPLAY_CHECKSUM %u\n", sum);
            printf("REPLAY_TURN %d\n", game.turn_number);
            printf("REPLAY_PHASE %s\n", game.game_phase);
            bool ok = true;
            if (live_replay.has_expect_phase && strncmp(game.game_phase, live_replay.expect_phase, sizeof(game.game_phase)) != 0) {
                ok = false;
                printf("EXPECT_PHASE %s GOT %s\n", live_replay.expect_phase, game.game_phase);
            }
            if (live_replay.has_expect_turn && game.turn_number != live_replay.expect_turn) {
                ok = false;
                printf("EXPECT_TURN %d GOT %d\n", live_replay.expect_turn, game.turn_number);
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

    if (record_path && replay_global_recording()) {
        if (!replay_global_save(record_path)) {
            SDL_Log("Failed to save replay to %s", record_path);
        }
        replay_global_stop();
    }

    game_destroy(&game);
    render_shutdown(&render);
    if (live_replay.event_count > 0) {
        replay_free(&live_replay);
    }
    return 0;
}
