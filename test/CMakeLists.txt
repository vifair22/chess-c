add_executable(chess-c-tests
    runner.c
    ../src/core/filesystem.c
)

target_include_directories(chess-c-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(chess-c-tests PRIVATE ${SDL2_LIB_TARGET} ${SDL2_MAIN_TARGET})

# Unit tests
add_executable(chess-c-unit-tests
    unit/test_main.c
    unit/test_state.c
    unit/test_chess_rules.c
    unit/test_moves.c
    ../src/game/state.c
    ../src/game/moves.c
    ../src/game/chess_rules.c
    ../src/game/checkers_rules.c
    ../src/game/logic.c
    ../src/ai/ai.c
    ../src/ai/search.c
    ../src/renderer/assets.c
    ../src/save/replay.c
    ../src/save/replay_parse.c
    ../src/save/save_handler.c
    ../src/save/save_sync.c
    ../src/core/filesystem.c
)

target_include_directories(chess-c-unit-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(chess-c-unit-tests PRIVATE ${SDL2_LIB_TARGET})
# Define SDL_MAIN_HANDLED to prevent SDL from redefining main
target_compile_definitions(chess-c-unit-tests PRIVATE SDL_MAIN_HANDLED)
