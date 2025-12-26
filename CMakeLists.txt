cmake_minimum_required(VERSION 3.14)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
cmake_policy(VERSION 3.14)

project(Suika4D)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Dependencies ---

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# SDL2
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.3
)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# SDL2_mixer
FetchContent_Declare(
    SDL2_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG release-2.6.3
)
set(SDL2_MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2_MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2_MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2_MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2_MIXER_MOD_MIKMOD OFF CACHE BOOL "" FORCE)
# Use both prefixes to be safe across different CMake script versions
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD_MIKMOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD_MODPLUG OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI_FLUIDSYNTH OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI_TIMIDITY OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI_NATIVE OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)  # IMPORTANT: Disable the format entirely
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE) # IMPORTANT: Disable the format entirely
FetchContent_MakeAvailable(SDL2_mixer)

# Assimp
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.2
)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(assimp)

# FreeType
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-2
)
set(FT_WITH_ZLIB ON CACHE BOOL "" FORCE)
set(FT_WITH_BZIP2 OFF CACHE BOOL "" FORCE)
set(FT_WITH_PNG OFF CACHE BOOL "" FORCE)
set(FT_WITH_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(FT_WITH_BROTLI OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)

# --- Core Libraries ---
add_library(STB_IMAGE STATIC src/stb_image.cpp)
target_include_directories(STB_IMAGE PRIVATE includes)
add_library(GLAD STATIC src/glad.c)
target_include_directories(GLAD PRIVATE includes)

# --- Executable ---
file(GLOB_RECURSE SOURCES 
    "src/4d_game/*.cpp" 
    "src/4d_game/*.h" 
    "src/4d_game/*.hpp"
    "src/4d_game/*.vs" 
    "src/4d_game/*.fs"
)
list(REMOVE_ITEM SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/4d_game/shader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/4d_game/shader.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/4d_game/resource_manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/4d_game/resource_manager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/4d_game/texture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/4d_game/texture.h"
)
add_executable(4d_game ${SOURCES})

target_include_directories(4d_game PRIVATE 
    includes
    ${CMAKE_BINARY_DIR}/configuration
)

# Configuration header
configure_file(configuration/root_directory.h.in configuration/root_directory.h)

# Link Libraries
target_link_libraries(4d_game PRIVATE
    GLAD
    STB_IMAGE
    glm::glm
    glfw
    assimp::assimp
    SDL2::SDL2-static
    SDL2_mixer::SDL2_mixer-static
    freetype
)

if(WIN32)
    target_link_libraries(4d_game PRIVATE opengl32)
    target_compile_definitions(4d_game PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
    target_link_libraries(4d_game PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreMIDI"
        "-framework OpenGL"
        "iconv"
    )
    set_target_properties(4d_game PROPERTIES 
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        MACOSX_BUNDLE_RESOURCES "${CMAKE_SOURCE_DIR}/resources"
    )
endif()

# Installation
install(TARGETS 4d_game
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
install(DIRECTORY resources/ DESTINATION resources)

# Packaging
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()
include(CPack)
