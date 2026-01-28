cmake_minimum_required(VERSION 3.20)

# If vcpkg is available, use it automatically when no toolchain is set.
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain")
endif()

project(chess-c LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CONFIGURATION_TYPES)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${config}" config_upper)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
    endforeach()
else()
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif()

find_package(SDL2 CONFIG QUIET)
if (NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED)
    if (NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 INTERFACE IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
        )
    endif()
    if (SDL2MAIN_LIBRARY AND NOT TARGET SDL2::SDL2main)
        add_library(SDL2::SDL2main INTERFACE IMPORTED)
        set_target_properties(SDL2::SDL2main PROPERTIES
            INTERFACE_LINK_LIBRARIES "${SDL2MAIN_LIBRARY}"
        )
    endif()
endif()

set(SDL2_LIB_TARGET SDL2::SDL2)
if (TARGET SDL2::SDL2-static)
    set(SDL2_LIB_TARGET SDL2::SDL2-static)
endif()

set(SDL2_MAIN_TARGET "")
if (TARGET SDL2::SDL2main)
    set(SDL2_MAIN_TARGET SDL2::SDL2main)
endif()

add_executable(chess-c
    src/main.c
    src/ai/ai.c
    src/ai/search.c
    src/game/logic.c
    src/renderer/draw.c
    src/renderer/render.c
    src/renderer/palette.c
    src/renderer/assets.c
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
if (MSVC)
    target_compile_options(chess-c PRIVATE $<$<CONFIG:Debug>:/analyze>)
endif()
target_link_libraries(chess-c PRIVATE ${SDL2_LIB_TARGET} ${SDL2_MAIN_TARGET})

add_subdirectory(test)

if (WIN32)
    # Match vcpkg/SDL2 default runtime (MSVC /MD, /MDd) to avoid LNK4098.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
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
