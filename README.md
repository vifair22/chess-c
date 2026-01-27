# Chess (SDL2, C)

Lightweight SDL2 chess + checkers game. Core gameplay, rendering, replay recording, and a replay-driven test runner live in a single C codebase (CMake + vcpkg).

## Features
- Chess and checkers rule sets (toggle from the main menu).
- Legal move generation (including castling, en passant, promotion, check/checkmate).
- Replay capture and deterministic playback (see `REPLAY_SCHEMA.md`).
- Headless/test mode for replay verification.
- Debug overlay (FPS/phase/turn) toggle.

## Build

### Windows (vcpkg)
```powershell
.\build_win.ps1 -Config Release -Triplet x64-windows-static
```

- Requires `VCPKG_ROOT` (or pass `-VcpkgRoot`).
- Builds to `build\` and copies `compile_commands.json` to the repo root.
- Outputs land in `build\Release\` or `build\Debug\` based on the config.
- Visual Studio users can open the folder and select the `VS (vcpkg)` preset.

### macOS / Linux
```bash
./build_unix.sh
```

- Builds to `build/`.
- Set `VCPKG_ROOT` to enable the vcpkg toolchain.
- Outputs land in `build/Release/` or `build/Debug/` based on `CONFIG`.

### Manual CMake
```bash
cmake -S . -B build
cmake --build build --config Release
```

For Debug builds:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

## Run

Windows (default config):
```powershell
.\build\Release\chess-c.exe
```

macOS/Linux:
```bash
./build/Release/chess-c
```

## Controls
- Left click: select/move.
- Enter/Space: start game from main menu / return to menu after game over.
- H / C (main menu): switch to chess / checkers.
- Esc / P: toggle pause.
- Q or R (pause menu): resign.
- Q / R / B / N (in game): set preferred promotion piece.
- F3: toggle debug overlay.

## Replay capture & playback

Record a session (saved on exit):
```bash
chess-c --record replays/my_game.rep
```

Replay interactively:
```bash
chess-c --replay replays/my_game.rep
```

Replay in headless/test mode (prints checksums and exits non-zero on failure):
```bash
chess-c --replay replays/my_game.rep --headless --test
```

The replay file format (including assertions for tests) is documented in `REPLAY_SCHEMA.md`.

## Tests

The CMake test runner (`test/chess-c-tests`) replays all `.rep` files in `replays/` and validates checksums/expectations.

```powershell
.\build\Release\chess-c-tests.exe --dir replays --exe .\build\Release\chess-c.exe
```

## Code layout
- `src/game/logic.*`: board state, rules, legal move generation, and game phases.
- `src/renderer/*`: SDL rendering, UI panels, piece sprites, debug overlay.
- `src/input/*`: keyboard + mouse input handling.
- `src/save/replay.*`: replay recording/loading, checksums, assertions.
- `src/main.c`: SDL init, main loop, replay CLI flags.

## Structs (expanded)

### Core board types
- `SquareState`: `piece` ('.' empty) + `flags` (legal/blocked/highlight/check markers).
- `BoardPos`: 0–7 file/rank, `-1` means unset.
- `BoardState`: `squares[8][8]` grid of `SquareState`.
- `MoveHint`: `flags` (castle/en passant/promo/capture/etc.) + `promotion`.
- `GameTime`: `total_seconds` elapsed (accumulated only while not paused).

Layout:
```
SquareState
├─ char piece               // '.' empty, piece code otherwise
└─ uint8_t flags            // SquareFlags bitmask

BoardPos
├─ int file                 // 0-7, -1 unset
└─ int rank                 // 0-7, -1 unset

BoardState
└─ SquareState squares[8][8]

MoveHint
├─ uint8_t flags            // MoveFlags bitmask
└─ char promotion           // promotion piece (uppercase), 0 if none

GameTime
└─ double total_seconds     // accumulated play time
```

### GameState (expanded)
- `mutex`: guards cross-thread access (render thread snapshots state).
- `board_state`: current board pieces + per-square flags.
- `match`: active mode/phase/turn/time (`mode`, `phase`, `white_to_move`, `turn_number`, `game_time`).
- `moves`: selection/hover state + `legal_moves[8][8]` for the current selection.
- `last_move`: last move endpoints (for animation).
- `variant`: checkers chain + promotion preferences.
- `chess`: castling/en-passant/halfmove + repetition history.
- `ui`: menu/debug toggles.
- `perf`: last frame timing + smoothed averages.

Layout:
```
GameState
├─ SDL_mutex *mutex
├─ BoardState board_state
│   └─ SquareState squares[8][8]
├─ MatchState match
│   ├─ char mode[16]
│   ├─ char phase[24]
│   ├─ bool white_to_move
│   ├─ int turn_number
│   └─ GameTime game_time
│       └─ double total_seconds
├─ MoveContext moves
│   ├─ Selection selected
│   │   ├─ int file
│   │   └─ int rank
│   ├─ BoardPos hover_pos
│   │   ├─ int file
│   │   └─ int rank
│   └─ MoveHint legal_moves[8][8]
│       ├─ uint8_t flags
│       └─ char promotion
├─ LastMove last_move
│   ├─ BoardPos start
│   │   ├─ int file
│   │   └─ int rank
│   └─ BoardPos end
│       ├─ int file
│       └─ int rank
├─ VariantState variant
│   ├─ bool checkers_must_continue
│   ├─ char promotion_choice_white
│   └─ char promotion_choice_black
├─ ChessRulesState chess
│   ├─ ChessRights rights
│   │   ├─ bool white_can_castle_kingside
│   │   ├─ bool white_can_castle_queenside
│   │   ├─ bool black_can_castle_kingside
│   │   ├─ bool black_can_castle_queenside
│   │   ├─ BoardPos en_passant_target
│   │   │   ├─ int file
│   │   │   └─ int rank
│   │   └─ int halfmove_clock
│   └─ PositionHistory history
│       ├─ uint64_t hashes[512]
│       └─ int len
├─ UiState ui
│   ├─ bool show_main_menu
│   ├─ bool show_pause_menu
│   └─ bool show_debug
└─ PerfState perf
    ├─ double last_frame_seconds
    ├─ double last_fps
    ├─ double smoothed_frame_ms
    ├─ double smoothed_fps
    ├─ double accum_time
    └─ int accum_frames
```

### Render structs
- `RenderContext`: SDL window/renderer, cached piece textures, animation state,
  last turn number, and per-square overlay fade buffers.
- `BoardGeometry`: board pixel origin + `cell` size + total `board_size`.
- `RenderPalette`: named UI colors for board, overlays, pieces, text, and panels.
- `PieceSprite`: embedded sprite metadata + RGBA pixels.
- `Glyph5x7`: 5x7 bitmap font glyph data.

Layout:
```
RenderContext
├─ RenderSurface surface
│   ├─ SDL_Window *window
│   ├─ SDL_Renderer *renderer
│   ├─ int window_width
│   └─ int window_height
├─ RenderAssets assets
│   └─ SDL_Texture *piece_textures[16]
├─ RenderAnim anim
│   ├─ bool active
│   ├─ char piece
│   ├─ int from_file
│   ├─ int from_rank
│   ├─ int to_file
│   ├─ int to_rank
│   ├─ Uint32 start_ms
│   ├─ Uint32 duration_ms
│   ├─ SDL_FPoint from_px
│   └─ SDL_FPoint to_px
├─ RenderOverlays overlays
│   ├─ float legal_alpha[8][8]
│   ├─ float highlight_alpha[8][8]
│   ├─ float blocked_alpha[8][8]
│   ├─ float hover_alpha[8][8]
│   └─ float check_alpha[8][8]
└─ int last_turn_number

BoardGeometry
├─ int offset_x
├─ int offset_y
├─ int cell
└─ int board_size

RenderPalette
├─ SDL_Color background
├─ SDL_Color board_light
├─ SDL_Color board_dark
├─ SDL_Color select_fill
├─ SDL_Color select_edge
├─ SDL_Color legal_fill
├─ SDL_Color hover_fill
├─ SDL_Color blocked_fill
├─ SDL_Color highlight_fill
├─ SDL_Color check_fill
├─ SDL_Color piece_light
├─ SDL_Color piece_dark
├─ SDL_Color hud_text
├─ SDL_Color label_text
├─ SDL_Color panel_backdrop
├─ SDL_Color panel_fill
├─ SDL_Color panel_border
└─ SDL_Color panel_text

PieceSprite
├─ const char *name
├─ uint8_t width
├─ uint8_t height
└─ const uint32_t *pixels

Glyph5x7
├─ char ch
└─ uint8_t rows[7]
```

### Replay structs
- `ReplayEvent`: timestamped input/assert event (click/hover/asserts) plus optional lists
  for `assert_legal` (allowed/forbidden squares).
- `ReplayStartState`: initial board setup, phase, castling/en-passant rights, and default
  promotion pieces.
- `ReplayRecorder`: in-memory capture buffer and starting snapshot for recording.
- `ReplayData`: loaded replay + optional end-state expectations (phase/turn/checksum/board).

Layout:
```
ReplayEvent
├─ uint32_t t_ms
├─ ReplayEventType type
├─ int8_t file
├─ int8_t rank
├─ int8_t aux_file
├─ int8_t aux_rank
├─ int32_t arg
├─ char text[24]
├─ int8_t allowed_files[64]
├─ int8_t allowed_ranks[64]
├─ int allowed_count
├─ int8_t forbidden_files[64]
├─ int8_t forbidden_ranks[64]
└─ int forbidden_count

ReplayStartState
├─ char game_mode[16]
├─ char game_phase[24]
├─ bool white_to_move
├─ int turn_number
├─ int halfmove_clock
├─ bool white_can_castle_kingside
├─ bool white_can_castle_queenside
├─ bool black_can_castle_kingside
├─ bool black_can_castle_queenside
├─ BoardPos en_passant_target
├─ char board[8][9]
├─ char promote_white
└─ char promote_black

ReplayRecorder
├─ ReplayStartState start
├─ ReplayEvent *events
├─ size_t event_count
├─ size_t event_capacity
├─ uint32_t start_ticks
└─ bool recording

ReplayData
├─ ReplayStartState start
├─ ReplayEvent *events
├─ size_t event_count
├─ size_t event_capacity
├─ bool has_expect_phase
├─ char expect_phase[24]
├─ bool has_expect_turn
├─ int expect_turn
├─ bool has_expect_checksum
├─ uint32_t expect_checksum
├─ bool has_expect_board
└─ char expect_board[8][9]
```
