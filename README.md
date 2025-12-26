## Suika Game in 4D!

<img width="1604" alt="game photo" src="footage/gameImage.png"> 

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

## Prerequisites

### Windows
1.  **CMake**: Install via PowerShell: `winget install kitware.cmake` OR download from [cmake.org](https://cmake.org/download/) (ensure "Add to PATH" is selected).
2.  **C++ Compiler**: 
    - **Recommended**: Install [Visual Studio Community](https://visualstudio.microsoft.com/vs/community/) and select **"Desktop development with C++"**.
    - **Alternative**: Install [MSYS2](https://www.msys2.org/) and run `pacman -S mine-w64-x86_64-toolchain`.

### macOS
Install dependencies via Homebrew:
```bash
brew install cmake assimp glm glfw freetype sdl2 sdl2_mixer
```

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install cmake build-essential libx11-dev libxcursor-dev libxinerama-dev libxrandr-dev libxi-dev libasound2-dev libmesa-dev libglu1-mesa-dev
```

## Quick Start (Local)
The project now uses a simplified build process with automated dependency management.

### Windows
To build and run on Windows (PowerShell):
```powershell
.\build.bat
.\build.bat run
```

### macOS / Linux
To build and run:
```bash
make build
make run
```

## Platform-Specific Building (CI/CD)
Automated builds for Windows and macOS are handled via GitHub Actions. The built artifacts are available in the GitHub Actions run results or as releases.

### Manual Build
If you prefer manual building with CMake:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```


# Asset Credits

credit to Chris Haugen for the theme (Morning Mandolin)[https://www.youtube.com/watch?v=jDQgLlmCivI]

credit to LAPDOR for (Floating Island)[https://sketchfab.com/3d-models/floating-island-3a5aca0e003d4effae29c107787e2012]
