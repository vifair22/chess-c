# Chess (SDL2, C)

Lightweight SDL2 starter for a chess game. Uses the same build setup as the breakout template: CMake, vcpkg, and a minimal code split between game state and rendering.

## Build

```bash
cmake -B build -S .
cmake --build build
```

- If you use vcpkg, set `VCPKG_ROOT` or add `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`.
- On macOS, the Debug config builds a console app; Release/RelWithDebInfo build an app bundle.

## Run

```bash
./build/chess-c
```

## Dev tooling

- Configure once to generate `build/compile_commands.json`. Point VS Code/clangd at it (or symlink it to the repo root) for IntelliSense.
- The game loop is split into `src/game.*` (state and input) and `src/render.*` (drawing). `src/main.c` wires SDL init and the event loop.

## Game state layout (example)

The unified `GameState` holds core board data, renderer metadata, and a mutex for cross-thread safety:

```c
typedef struct {
    SDL_mutex *mutex;
    BoardState board_state; // see below
    bool white_to_move;
    int selected_file; // 0-7, -1 unset
    int selected_rank; // 0-7, -1 unset
    BoardPos hover_pos; // current hover/preview square

    char game_mode[16]; // e.g., "chess", "checkers", "ataxx"
    int turn_number;
    GameTime game_time;
    char game_phase[16]; // e.g., "play", "pause", "menu"

    BoardPos move_start; // file/rank, -1 when unset
    BoardPos move_end;   // file/rank, -1 when unset
} GameState;

typedef struct {
    SquareState squares[8][8];
} BoardState;

typedef struct {
    double total_seconds; // accumulated game time
} GameTime;

typedef struct {
    char piece;    // '.' empty, otherwise piece code
    uint8_t flags; // bitmask
} SquareState;

typedef struct {
    int file; // 0-7, -1 when unset
    int rank; // 0-7, -1 when unset
} BoardPos;
```

`game_init` creates the mutex, fills the starting position into `board_state`, and zeroes metadata. The renderer locks `mutex`, snapshots the state, and draws from the snapshot.

ASCII layout of the struct:

```
GameState
├─ SDL_mutex *mutex          // guards shared state
├─ bool white_to_move
├─ int selected_file         // 0-7, -1 unset
├─ int selected_rank         // 0-7, -1 unset
├─ BoardPos hover_pos        // current hover/preview square
├─ BoardState board_state    // per-square metadata
│   └─ SquareState squares[8][8]
│       ├─ char piece        // '.' empty, else piece code
│       └─ uint8_t flags     // bitmask (blocked/legal/highlight)
├─ char game_mode[16]
├─ int turn_number
├─ GameTime game_time        // accumulated time
├─ char game_phase[16]       // e.g., "play", "pause", "menu", etc.
├─ BoardPos move_start
│   ├─ int file              // 0-7, -1 unset
│   └─ int rank              // 0-7, -1 unset
└─ BoardPos move_end
    ├─ int file              // 0-7, -1 unset
    └─ int rank              // 0-7, -1 unset
```
