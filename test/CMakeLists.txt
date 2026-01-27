add_executable(chess-c-tests
    runner.c
)

target_include_directories(chess-c-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(chess-c-tests PRIVATE ${SDL2_LIB_TARGET} ${SDL2_MAIN_TARGET})
