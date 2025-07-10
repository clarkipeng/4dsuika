#!/bin/bash
set -euo pipefail

# --- Configuration ---
ROOT_DIR="$(pwd)"
BUILD_DIR="$ROOT_DIR/build"
VENDOR_DIR="$ROOT_DIR/vendor"
MACOS_INSTALL_DIR="$BUILD_DIR/macos_install"
WIN_INSTALL_DIR="$BUILD_DIR/windows_install"
TOOLCHAIN_FILE="$ROOT_DIR/cmake/windows_toolchain.cmake" # Assumes this path is correct

# Dependency Versions
GLFW_VER="3.3.8"
SDL2_VER="2.30.3"
SDL2_MIXER_VER="2.6.3"
ASSIMP_VER="5.4.2"
FREETYPE_VER="VER-2-13-2" # Use a specific tag for freetype
GLM_VER="1.0.1"

# Use the correct command for macOS to get core count
NUM_CORES=$(sysctl -n hw.logicalcpu)

# --- Initial Setup ---
echo "🔹 Initializing directories..."
mkdir -p "$VENDOR_DIR"
mkdir -p "$MACOS_INSTALL_DIR"
mkdir -p "$WIN_INSTALL_DIR"

# Guide user if no argument is provided
if [ "$#" -eq 0 ]; then
    echo "Usage: $0 <macos|windows>"
    exit 1
fi

# --- Clone Dependencies ---
echo "🔹 Cloning all dependencies if they don't exist..."
cd "$VENDOR_DIR"
[ ! -d "glfw" ]      && git clone --branch "$GLFW_VER" --depth 1 https://github.com/glfw/glfw.git
[ ! -d "SDL" ]       && git clone --branch "release-$SDL2_VER" --depth 1 https://github.com/libsdl-org/SDL.git SDL
# [ ! -d "SDL_mixer" ] && git clone --branch "release-$SDL2_MIXER_VER" --depth 1 https://github.com/libsdl-org/SDL_mixer.git SDL_mixer
echo "  > Force-cloning a clean copy of SDL_mixer..."
rm -rf SDL_mixer
git clone --branch "release-$SDL2_MIXER_VER" --depth 1 https://github.com/libsdl-org/SDL_mixer.git SDL_mixer
[ ! -d "assimp" ]    && git clone --branch "v$ASSIMP_VER" --depth 1 https://github.com/assimp/assimp.git
[ ! -d "freetype" ]  && git clone --branch "$FREETYPE_VER" --depth 1 https://github.com/freetype/freetype.git
[ ! -d "glm" ]       && git clone --branch "$GLM_VER" --depth 1 https://github.com/g-truc/glm.git glm
echo "✅ All dependencies cloned."


# =================================================================
# == BUILD FOR MACOS (NATIVE)
# =================================================================
if [ "$1" == "macos" ]; then
    INSTALL_DIR="$MACOS_INSTALL_DIR"
    BUILD_SUFFIX="macos"

    echo "🔥 Cleaning previous macOS build artifacts..."
    rm -rf "$INSTALL_DIR"
    rm -rf "$BUILD_DIR/macos"
    mkdir -p "$INSTALL_DIR"

    [ ! -d "bzip2" ]     && git clone --depth 1 https://github.com/libarchive/bzip2.git
    [ ! -d "libpng" ]    && git clone --branch v1.6.43 --depth 1 https://github.com/glennrp/libpng.git
    [ ! -d "brotli" ]    && git clone --depth 1 https://github.com/google/brotli.git
    echo "  🔹 Building BZip2 for macOS..."
    cmake -S "$VENDOR_DIR/bzip2" -B "$VENDOR_DIR/bzip2/build-$BUILD_SUFFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DENABLE_SHARED=OFF \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/bzip2/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/bzip2/build-$BUILD_SUFFIX"

    echo "  🔹 Building libpng for macOS..."
    cmake -S "$VENDOR_DIR/libpng" -B "$VENDOR_DIR/libpng/build-$BUILD_SUFFIX" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/libpng/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/libpng/build-$BUILD_SUFFIX"

    echo "  🔹 Building Brotli for macOS..."
    cmake -S "$VENDOR_DIR/brotli" -B "$VENDOR_DIR/brotli/build-$BUILD_SUFFIX" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/brotli/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/brotli/build-$BUILD_SUFFIX"

    echo "🚀 Building all dependencies for macOS..."

    echo "  🔹 Building GLFW for macOS..."
    cmake -S "$VENDOR_DIR/glfw" -B "$VENDOR_DIR/glfw/build-$BUILD_SUFFIX" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/glfw/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/glfw/build-$BUILD_SUFFIX"

    echo "  🔹 Building Assimp for macOS..."
    cmake -S "$VENDOR_DIR/assimp" -B "$VENDOR_DIR/assimp/build-$BUILD_SUFFIX" -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/assimp/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/assimp/build-$BUILD_SUFFIX"

    echo "  🔹 Building FreeType for macOS..."
    cmake -S "$VENDOR_DIR/freetype" -B "$VENDOR_DIR/freetype/build-$BUILD_SUFFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_PREFIX_PATH="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/freetype/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/freetype/build-$BUILD_SUFFIX"

    echo "  🔹 Building SDL2 for macOS..."
    cmake -S "$VENDOR_DIR/SDL" -B "$VENDOR_DIR/SDL/build-$BUILD_SUFFIX" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/SDL/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/SDL/build-$BUILD_SUFFIX"

    echo "  🔹 Building SDL2_mixer for macOS..."
    (
        cd "$VENDOR_DIR/SDL_mixer"
        ./external/download.sh
        echo "    > Patching vendored CMake files..."
        sed -i '' 's/cmake_minimum_required(VERSION 3.1)/cmake_minimum_required(VERSION 3.5)/' external/opus/CMakeLists.txt
        sed -i '' 's/cmake_minimum_required(VERSION 3.1)/cmake_minimum_required(VERSION 3.5)/' external/opus/opus_functions.cmake
        sed -i '' 's/cmake_minimum_required(VERSION 2.8.12)/cmake_minimum_required(VERSION 3.5)/' external/libmodplug/CMakeLists.txt
    )
    cmake -S "$VENDOR_DIR/SDL_mixer" -B "$VENDOR_DIR/SDL_mixer/build-$BUILD_SUFFIX" \
        -DBUILD_SHARED_LIBS=OFF -DSDL2MIXER_VENDORED=ON \
        -DCMAKE_PREFIX_PATH="$INSTALL_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/SDL_mixer/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/SDL_mixer/build-$BUILD_SUFFIX"

    echo "✅ All macOS dependencies are installed."
    echo "🚀 Building main project for macOS..."
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR/macos" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
        -DGLM_INCLUDE_DIR="$VENDOR_DIR/glm" \
        -DMACOS_INSTALL_DIR="$INSTALL_DIR" \
        -DLINK_EXTRA_LIBS=ON
    cmake --build "$BUILD_DIR/macos" -j"$NUM_CORES"
    (cd "$BUILD_DIR/macos" && make package)
    echo "✅ macOS build complete! Executable is in $BUILD_DIR/macos/"
fi

# =================================================================
# == BUILD FOR WINDOWS (CROSS-COMPILE)
# =================================================================
if [ "$1" == "windows" ]; then
    echo "🚀 Cross-compiling all dependencies for Windows..."
    INSTALL_DIR="$WIN_INSTALL_DIR"
    BUILD_SUFFIX="windows"
    
    if [ ! -f "$TOOLCHAIN_FILE" ]; then
        echo "❌ Windows toolchain file not found at $TOOLCHAIN_FILE"
        exit 1
    fi

    # -- Build each dependency for Windows ---
    echo "  🔹 Building GLFW for Windows..."
    cmake -S "$VENDOR_DIR/glfw" -B "$VENDOR_DIR/glfw/build-$BUILD_SUFFIX" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/glfw/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/glfw/build-$BUILD_SUFFIX"

    echo "  🔹 Building Assimp for Windows..."
    cmake -S "$VENDOR_DIR/assimp" -B "$VENDOR_DIR/assimp/build-$BUILD_SUFFIX" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/assimp/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/assimp/build-$BUILD_SUFFIX"

    echo "  🔹 Building FreeType for Windows..."
    cmake -S "$VENDOR_DIR/freetype" -B "$VENDOR_DIR/freetype/build-$BUILD_SUFFIX" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/freetype/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/freetype/build-$BUILD_SUFFIX"
    
    echo "  🔹 Building SDL2 for Windows..."
    cmake -S "$VENDOR_DIR/SDL" -B "$VENDOR_DIR/SDL/build-$BUILD_SUFFIX" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build "$VENDOR_DIR/SDL/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/SDL/build-$BUILD_SUFFIX"

    echo "  🔹 Building SDL2_mixer for Windows..."
    (
        cd "$VENDOR_DIR/SDL_mixer"
        # This part for downloading dependencies is fine
        ./external/download.sh
        echo "    > Patching vendored CMake files..."
        sed -i '' 's/cmake_minimum_required(VERSION 3.1)/cmake_minimum_required(VERSION 3.5)/' external/opus/CMakeLists.txt
        sed -i '' 's/cmake_minimum_required(VERSION 3.1)/cmake_minimum_required(VERSION 3.5)/' external/opus/opus_functions.cmake
        sed -i '' 's/cmake_minimum_required(VERSION 2.8.12)/cmake_minimum_required(VERSION 3.5)/' external/libmodplug/CMakeLists.txt
    )
    cmake -S "$VENDOR_DIR/SDL_mixer" -B "$VENDOR_DIR/SDL_mixer/build-$BUILD_SUFFIX" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DBUILD_SHARED_LIBS=OFF -DSDL2MIXER_VENDORED=ON \
        -DCMAKE_PREFIX_PATH="$INSTALL_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

    cmake --build "$VENDOR_DIR/SDL_mixer/build-$BUILD_SUFFIX" -j"$NUM_CORES"
    cmake --install "$VENDOR_DIR/SDL_mixer/build-$BUILD_SUFFIX"

    # -- Build Main Project for Windows --
    echo "✅ All Windows dependencies are installed."
    echo "🚀 Cross-compiling main project for Windows..."
    cd "$ROOT_DIR"
    cmake -S . -B "$BUILD_DIR/windows" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$WIN_INSTALL_DIR" \
        -DSDL_MAIN_HANDLED=ON \
        -DFREETYPE_INCLUDE_DIRS="$WIN_INSTALL_DIR/include/freetype2" \
        -DFREETYPE_LIBRARY="$WIN_INSTALL_DIR/lib/libfreetype.a" \
        -DZLIB_INCLUDE_DIR="$WIN_INSTALL_DIR/include" \
        -DZLIB_LIBRARY="$WIN_INSTALL_DIR/lib/libzlibstatic.a" \
        -DGLM_INCLUDE_DIR="$VENDOR_DIR/glm" \
        -DGLFW3_INCLUDE_DIR="$WIN_INSTALL_DIR/include" \
        -DGLFW3_LIBRARY="$WIN_INSTALL_DIR/lib/libglfw3.a" \
        -DASSIMP_INCLUDE_DIR="$WIN_INSTALL_DIR/include" \
        -DASSIMP_LIBRARY="$WIN_INSTALL_DIR/lib/libassimp.a"
    cmake --build "$BUILD_DIR/windows" -j"$NUM_CORES"
    (cd "$BUILD_DIR/windows" && make package)
    echo "✅ Windows build complete! Executable is in $BUILD_DIR/windows/"
fi