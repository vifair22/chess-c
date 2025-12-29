cmake_minimum_required(VERSION 3.20)

project(chess-c LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 CONFIG REQUIRED)

add_executable(chess-c
    src/main.c
    src/game/logic.c
    src/renderer/draw.c
    src/renderer/render.c
    src/renderer/palette.c
    src/renderer/ui.c
    src/renderer/debug_overlay.c
    src/input/keyboard.c
    src/input/mouse.c
    src/save/save_handler.c
    src/save/save_sync.c
    src/save/replay.c
    src/audio/audio.c
)

target_include_directories(chess-c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(chess-c PRIVATE $<$<CONFIG:Debug>:DEBUG>)
target_link_libraries(chess-c PRIVATE SDL2::SDL2-static SDL2::SDL2main)

if (WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(chess-c PRIVATE
        $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
        $<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>
    )
endif()

if (APPLE)
    set_target_properties(chess-c PROPERTIES
        MACOSX_BUNDLE $<$<NOT:$<CONFIG:Debug>>:ON>
    )
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(chess-c PRIVATE m)
endif()
