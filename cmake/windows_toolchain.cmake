# windows-toolchain.cmake

# 1. Set the target system name to Windows
set(CMAKE_SYSTEM_NAME Windows)

# 2. Specify the cross-compilers for C and C++
#    Use the 'which' command in your terminal to find the exact paths if needed.
set(CMAKE_C_COMPILER   /opt/homebrew/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /opt/homebrew/bin/x86_64-w64-mingw32-g++)

# 3. (Optional) Set the resource compiler
set(CMAKE_RC_COMPILER  /opt/homebrew/bin/x86_64-w64-mingw32-windres)

# 4. (Optional) Point to the root of the target environment for libraries and headers
#    This helps CMake find MinGW-w64 libraries correctly.
set(CMAKE_FIND_ROOT_PATH /opt/homebrew/opt/mingw-w64/toolchain-x86_64)

# 5. Configure search behavior for programs, libraries, and headers
#    This ensures CMake only finds target-specific tools and libraries.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)