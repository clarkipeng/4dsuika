## Suika Game in 4D!
This repository was forked from the LearnOpenGL [github repo](https://github.com/JoeyDeVries/LearnOpenGL)

## Mac OS X building
Building on Mac OS X is fairly simple:
```
brew install cmake assimp glm glfw freetype sdl2 sdl2_mixer
cmake -S . -B build -DCMAKE_EXE_LINKER_FLAGS="-L$(brew --prefix)/lib"
cmake --build build -j$(sysctl -n hw.logicalcpu)
```
To run locally, run the executable located at
```
/bin/9.4d_game
```

## Application Build
To statically build an application run the following for windows
```
bash build.sh windows
```
Run the following for macos
```
bash build.sh macos
```