@echo off
set BUILD_DIR=build

if "%1"=="clean" (
    echo Cleaning build directory...
    if exist %BUILD_DIR% rd /s /q %BUILD_DIR%
    exit /b 0
)

if not exist %BUILD_DIR% mkdir %BUILD_DIR%

echo Configuring CMake...
cmake -S . -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed.
    exit /b %ERRORLEVEL%
)

echo Building project...
cmake --build %BUILD_DIR% --config Release -j 8

if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b %ERRORLEVEL%
)

if "%1"=="run" (
    echo Running 4d_game...
    .\%BUILD_DIR%\Release\4d_game.exe
)

echo Done.
