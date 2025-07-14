## Suika Game in 4D!
This repository was forked from the LearnOpenGL [github repo](https://github.com/JoeyDeVries/LearnOpenGL)

The itch.io page is [here](https://werus23.itch.io/4d-suika).

# Demo & screenshots

[![Video Demo](https://img.youtube.com/vi/5Bj0ZGDzans/0.jpg)](https://youtu.be/5Bj0ZGDzans "4D Suika Game | Video Demo")

| | |
|:-------------------------:|:-------------------------:|
|<img width="1604" alt="screen shot1" src="footage/Screenshot 2025-07-13 at 2.07.38 PM.png"> |  <img width="1604" alt="screen shot2" src="footage/Screenshot 2025-07-13 at 2.16.44 PM.png">|
|<img width="1604" alt="screen shot3" src="footage/Screenshot 2025-07-13 at 2.17.11 PM.png"> |  <img width="1604" alt="screen shot4" src="footage/Screenshot 2025-07-13 at 2.17.56 PM.png">|

<!-- <object width="425" height="350">
  <param name="movie" value="https://youtu.be/5Bj0ZGDzans" />
  <param name="wmode" value="transparent" />
  <embed src="https://youtu.be/5Bj0ZGDzans"
         type="application/x-shockwave-flash"
         wmode="transparent" width="425" height="350" />
</object> -->

## Mac OS X building
Building on Mac OS X is fairly simple:
```
brew install cmake assimp glm glfw freetype sdl2 sdl2_mixer
cmake -S . -B build -DCMAKE_EXE_LINKER_FLAGS="-L$(brew --prefix)/lib"
cmake --build build -j$(sysctl -n hw.logicalcpu)
```
To run locally, run the executable located at
```
/build/9.4d_game
```
Zip using 
```
ditto -c -k --sequesterRsrc --keepParent build/macos/4d_game_.app 4d_game-macOS.zip
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


Asset Credits
credit to Chris Haugen for the theme Morning Mandolin https://www.youtube.com/watch?v=jDQgLlmCivI
credit to LAPDOR for Floating Island https://sketchfab.com/3d-models/floating-island-3a5aca0e003d4effae29c107787e2012 