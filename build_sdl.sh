#!/usr/bin/env bash
set -e
cd vendor/SDL2_mixer-2.6.0
mkdir -p build && cd build
cmake .. \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DSDL2MIXER_EXAMPLES=OFF \
  -DSDL2MIXER_TEST=OFF \
  -DSDL2MIXER_MUSIC_MID=OFF \
  -DSDL2MIXER_MUSIC_FLAC=OFF \
  -DSDL2MIXER_MUSIC_MOD=OFF \
  -DSDL2MIXER_MUSIC_OPUS=OFF \
  -DSDL2MIXER_MUSIC_OGG=OFF
cmake --build . --config Release
# resulting lib: vendor/SDL2_mixer-2.6.0/build/libSDL2_mixer.a
