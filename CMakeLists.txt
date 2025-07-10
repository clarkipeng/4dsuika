cmake_minimum_required(VERSION 3.10)
cmake_policy(VERSION 3.10)

project(LearnOpenGL)

option(LINK_EXTRA_MAC_LIBS "Link PNG and Brotli libraries" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

#-----------------------------------------------------------------------------
# 1. FIND DEPENDENCIES
#-----------------------------------------------------------------------------
add_library(STB_IMAGE STATIC src/stb_image.cpp)
add_library(GLAD STATIC src/glad.c)

find_package(GLM REQUIRED)
find_package(GLFW3 REQUIRED)
find_package(ASSIMP REQUIRED)
find_package(OpenGL REQUIRED)

set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)
find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
find_package(SDL2_mixer REQUIRED)
if(APPLE)
  find_package(BZip2 REQUIRED)
  find_package(PNG REQUIRED)
endif()

#-----------------------------------------------------------------------------
# 2. DEFINE LIBRARIES FOR LINKING (PLATFORM-SPECIFIC)
#-----------------------------------------------------------------------------
set(COMMON_LIBS
    GLAD
    STB_IMAGE
    ${ASSIMP_LIBRARY}
    ${GLFW3_LIBRARY}
    Freetype::Freetype
    SDL2_mixer::SDL2_mixer-static
    SDL2::SDL2-static
    ${ZLIB_LIBRARY}
)


if(WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  set(LIBS ${COMMON_LIBS} opengl32 mingw32)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mconsole")
elseif(APPLE)
  # Find all required macOS frameworks correctly
  find_library(COCOA_LIBRARY Cocoa)
  find_library(IOKit_LIBRARY IOKit)
  find_library(CoreVideo_LIBRARY CoreVideo)
  find_library(ICONV_LIBRARY iconv)
  find_library(COREAUDIO_FRAMEWORK CoreAudio)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
  find_library(AUDIOUNIT_FRAMEWORK AudioUnit)
  find_library(COREMIDI_FRAMEWORK CoreMIDI)
  find_library(GME_FRAMEWORK gme)

  # Create a list of all found frameworks and libraries
  set(APPLE_LIBS_AND_FRAMEWORKS
    ${COCOA_LIBRARY}
    ${IOKit_LIBRARY}
    ${CoreVideo_LIBRARY}
    ${OPENGL_LIBRARY}
    ${COREAUDIO_FRAMEWORK}
    ${AUDIOTOOLBOX_FRAMEWORK}
    ${AUDIOUNIT_FRAMEWORK}
    ${COREMIDI_FRAMEWORK}
    ${ICONV_LIBRARY}
    ${GME_FRAMEWORK}
    "bz2"
  )
  if(LINK_EXTRA_MAC_LIBS)
    message(STATUS "Extra libraries enabled: PNG and Brotli will be linked.")
    list(APPEND APPLE_LIBS_AND_FRAMEWORKS
        "${MACOS_INSTALL_DIR}/lib/libpng.a"
        "${MACOS_INSTALL_DIR}/lib/libbrotlidec.a"
        "${MACOS_INSTALL_DIR}/lib/libbrotlicommon.a"
    )
  else()
    message(STATUS "Linking against system DYNAMIC libraries.")
    # Link the libraries by name for the linker to find on the system
    list(APPEND APPLE_LIBS_AND_FRAMEWORKS
        "png"
        "brotlidec"
        "brotlicommon"
    )
  endif()

  # Add the found items to the main LIBS variable
  set(LIBS ${COMMON_LIBS} ${APPLE_LIBS_AND_FRAMEWORKS})

endif()


#-----------------------------------------------------------------------------
# 3. DEFINE YOUR PROJECT EXECUTABLES AND FUNCTIONS
#-----------------------------------------------------------------------------
include_directories(${CMAKE_SOURCE_DIR}/includes)
configure_file(configuration/root_directory.h.in configuration/root_directory.h)
include_directories(${CMAKE_BINARY_DIR}/configuration)

function(create_project_from_sources chapter demo)
  # This part finds all the source files for a given target
  file(GLOB SOURCE
      "src/${chapter}/${demo}/*.h"
      "src/${chapter}/${demo}/*.cpp"
      "src/${chapter}/${demo}/*.vs"
      "src/${chapter}/${demo}/*.fs"
      "src/${chapter}/${demo}/*.gs"
      "src/${chapter}/${demo}/*.cs"
  )

  # This part creates the executable's name
  if (demo STREQUAL "")
    string(REPLACE "/" "_" NAME ${chapter})
  else()
    set(NAME "${chapter}__${demo}")
  endif()

  # Creates the executable from the source files it found
  add_executable(${NAME} ${SOURCE})

  # Links all necessary libraries
  target_link_libraries(${NAME} ${LIBS})

  # Sets the output directory and fixes the Windows linker error
  if(WIN32)
    target_link_options(${NAME} PRIVATE -mconsole)
    set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/windows/${chapter}")
  elseif(APPLE)
    set_target_properties(${NAME} PROPERTIES MACOSX_BUNDLE TRUE)
    set_target_properties(${NAME} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
  endif()
endfunction()

set(GAME_ARTICLES
  9.4d_game/
)
foreach(GAME_ARTICLE ${GAME_ARTICLES})
  create_project_from_sources(${GAME_ARTICLE} "")
endforeach()


#-----------------------------------------------------------------------------
# 4. PACKAGING CONFIGURATION
#-----------------------------------------------------------------------------
# This section has been updated to correctly create distributable applications.

# The target name from your function is "9.4d_game_", not "9.4d_game".
set(GAME_TARGET 9.4d_game_)

# Install game resources like images, sounds, etc.
install(DIRECTORY resources/ DESTINATION resources)


# Set the CPack generator based on the platform BEFORE including CPack
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()

# Include the CPack module for packaging
include(CPack)
include(InstallRequiredSystemLibraries)

# --- Windows .zip Configuration ---
if(WIN32)
    # Install the executable to the root of the package
    install(TARGETS ${GAME_TARGET}
        RUNTIME DESTINATION .
        COMPONENT Applications
    )
    # Automatically find and include required MinGW DLLs
    set(CPACK_SYSTEM_RUNTIME_LIBS_SKIP_SYSTEM TRUE)
    if(EXISTS "/opt/homebrew/opt/mingw-w64/bin")
      set(CPACK_SYSTEM_RUNTIME_LIBS_PATH "/opt/homebrew/opt/mingw-w64/bin")
    endif()

# --- macOS .app Bundle Configuration ---
elseif(APPLE)
    # Install the .app bundle created by MACOSX_BUNDLE property
    install(TARGETS ${GAME_TARGET} BUNDLE DESTINATION .)
    
    # Install the resources directory into the .app bundle's Resources folder
    install(DIRECTORY resources/
        DESTINATION "${GAME_TARGET}.app/Contents/Resources"
    )

    # Configure the .dmg package properties
    set(CPACK_BUNDLE_NAME "${PROJECT_NAME}")
    set(CPACK_BUNDLE_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
endif()
