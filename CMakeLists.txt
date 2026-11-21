cmake_minimum_required(VERSION 3.14)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
cmake_policy(VERSION 3.14)

# Release builds for players (on by default in GitHub Actions): every
# dependency built from source and linked statically, never taken from the
# machine (Homebrew dylibs would not exist on a player's Mac), and one
# universal app for Apple Silicon and Intel Macs.
if(DEFINED ENV{GITHUB_ACTIONS})
    set(SUIKA_PORTABLE_DEFAULT ON)
else()
    set(SUIKA_PORTABLE_DEFAULT OFF)
endif()
option(SUIKA_PORTABLE "Self-contained release build" ${SUIKA_PORTABLE_DEFAULT})
if(SUIKA_PORTABLE)
    # PNG, BZip2, HarfBuzz and Brotli are freetype's optional extras (off below).
    foreach(pkg glfw3 SDL2 SDL2_mixer Freetype PNG BZip2 HarfBuzz BrotliDec)
        set(CMAKE_DISABLE_FIND_PACKAGE_${pkg} ON)
    endforeach()
    if(CMAKE_HOST_APPLE)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    endif()
endif()

project(Suika4D VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Dependencies ---

find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

if(NOT EMSCRIPTEN)
    find_package(glfw3 CONFIG QUIET)
    if(TARGET glfw)
        set(SUIKA_GLFW_TARGET glfw)
    else()
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
        set(SUIKA_GLFW_TARGET glfw)
    endif()

    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        set(SUIKA_SDL2_TARGET SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        set(SUIKA_SDL2_TARGET SDL2::SDL2-static)
    else()
        FetchContent_Declare(
            SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-2.30.3
        )
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL2)
        set(SUIKA_SDL2_TARGET SDL2::SDL2-static)
    endif()

    find_package(SDL2_mixer CONFIG QUIET)
    if(TARGET SDL2_mixer::SDL2_mixer)
        set(SUIKA_SDL2_MIXER_TARGET SDL2_mixer::SDL2_mixer)
    elseif(TARGET SDL2_mixer::SDL2_mixer-static)
        set(SUIKA_SDL2_MIXER_TARGET SDL2_mixer::SDL2_mixer-static)
    else()
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
        set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_MOD_MIKMOD OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_MOD_MODPLUG OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_MIDI_FLUIDSYNTH OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_MIDI_TIMIDITY OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_MIDI_NATIVE OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL2_mixer)
        set(SUIKA_SDL2_MIXER_TARGET SDL2_mixer::SDL2_mixer-static)
    endif()

    find_package(Freetype QUIET)
    if(TARGET Freetype::Freetype)
        set(SUIKA_FREETYPE_TARGET Freetype::Freetype)
    elseif(TARGET freetype)
        set(SUIKA_FREETYPE_TARGET freetype)
    else()
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
        foreach(ft PNG BZIP2 HARFBUZZ BROTLI)
            set(FT_DISABLE_${ft} ON CACHE BOOL "" FORCE)
        endforeach()
        FetchContent_MakeAvailable(freetype)
        set(SUIKA_FREETYPE_TARGET freetype)
    endif()
endif()

# --- Core Libraries ---
if(NOT EMSCRIPTEN)
    add_library(GLAD STATIC src/glad.c)
    target_include_directories(GLAD PRIVATE includes)
endif()

# --- Executable ---
file(GLOB_RECURSE SOURCES 
    "src/4d_game/*.cpp" 
    "src/4d_game/*.h" 
    "src/4d_game/*.hpp"
    "src/4d_game/*.vs" 
    "src/4d_game/*.fs"
)
add_executable(4d_game ${SOURCES})

target_include_directories(4d_game PRIVATE 
    src/platform
    includes
    ${CMAKE_BINARY_DIR}/configuration
)

# Configuration header
configure_file(configuration/root_directory.h.in configuration/root_directory.h)

# Link Libraries
target_link_libraries(4d_game PRIVATE
    glm::glm
)

if(EMSCRIPTEN)
    target_link_options(4d_game PRIVATE
        "-sUSE_GLFW=3"
        "-sFULL_ES3=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "--preload-file" "${CMAKE_SOURCE_DIR}/resources@resources"
        "--shell-file" "${CMAKE_SOURCE_DIR}/web_template.html"
    )
    set_target_properties(4d_game PROPERTIES
        OUTPUT_NAME "4DGame_Web"
        SUFFIX ".html"
    )
else()
    target_link_libraries(4d_game PRIVATE
        GLAD
        ${SUIKA_GLFW_TARGET}
        ${SUIKA_SDL2_TARGET}
        ${SUIKA_SDL2_MIXER_TARGET}
        ${SUIKA_FREETYPE_TARGET}
    )
endif()

if(EMSCRIPTEN)
    # Browser build is configured entirely by the Emscripten link options above.
elseif(WIN32)
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

# Installation: only the game goes in the release zip (component "game"),
# not the headers and libraries the fetched dependencies install. The game
# finds resources/ next to the executable, or inside the macOS bundle.
install(TARGETS 4d_game
    BUNDLE DESTINATION . COMPONENT game
    RUNTIME DESTINATION . COMPONENT game
)
if(APPLE)
    install(DIRECTORY resources/ DESTINATION 4d_game.app/Contents/Resources/resources COMPONENT game)
else()
    install(DIRECTORY resources/ DESTINATION resources COMPONENT game)
endif()
if(WIN32)
    # Ship the MSVC runtime so the game starts without the VC++ redistributable.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT game)
    include(InstallRequiredSystemLibraries)
endif()

# Packaging: 4D-Suika-<version>-windows.zip / -macos.zip
if(WIN32 OR APPLE)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL game)
    if(WIN32)
        set(CPACK_ARCHIVE_GAME_FILE_NAME "4D-Suika-${PROJECT_VERSION}-windows")
    else()
        set(CPACK_ARCHIVE_GAME_FILE_NAME "4D-Suika-${PROJECT_VERSION}-macos")
    endif()
    # Smoke test the zip as a player gets it, right after cpack makes it: a
    # 120 s scripted playtest from the unpacked copy, ignoring the source tree.
    # On by default in CI on macOS; Windows runners have no OpenGL 3.3.
    if(APPLE AND DEFINED ENV{GITHUB_ACTIONS})
        set(SUIKA_SMOKE_DEFAULT ON)
    else()
        set(SUIKA_SMOKE_DEFAULT OFF)
    endif()
    option(SUIKA_SMOKE "Smoke test the release zip after packaging" ${SUIKA_SMOKE_DEFAULT})
    if(SUIKA_SMOKE)
        set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/cmake/smoke-zip.cmake")
    endif()
endif()
include(CPack)
