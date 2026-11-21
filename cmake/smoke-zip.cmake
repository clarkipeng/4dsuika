# CPack post-build script (CPACK_POST_BUILD_SCRIPTS): unpack each release zip
# into a fresh folder and run a 120 s headless scripted playtest from it, with
# SUIKA_SHIPPED=1 so the game ignores the source tree baked in at configure
# time and must find the zip's own resources. It renders a frame every 10
# ticks (frames saved beside the unpacked copy) and fails packaging on a bad
# exit, any ERROR line (missing shader, font or sound) or a missing frame.
foreach(zip IN LISTS CPACK_PACKAGE_FILES)
    get_filename_component(name "${zip}" NAME)
    string(REGEX REPLACE "\\.zip$" "" name "${name}")
    set(dir "${CPACK_TOPLEVEL_DIRECTORY}/smoke/${name}")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${zip}" WORKING_DIRECTORY "${dir}" RESULT_VARIABLE rc)
    if(rc)
        message(FATAL_ERROR "smoke: could not unpack ${zip}")
    endif()
    file(GLOB_RECURSE exe LIST_DIRECTORIES false "${dir}/*4d_game.exe" "${dir}/*4d_game")
    if(NOT exe)
        message(FATAL_ERROR "smoke: no game executable in ${zip}")
    endif()
    list(GET exe 0 exe)
    get_filename_component(exeDir "${exe}" DIRECTORY)
    if(NOT EXISTS "${exeDir}/resources/shaders/raymarch4d.fs" AND NOT EXISTS "${exeDir}/../Resources/resources/shaders/raymarch4d.fs")
        message(FATAL_ERROR "smoke: no resources/ beside ${exe} or in its bundle")
    endif()
    set(frames "${dir}-frames")
    file(MAKE_DIRECTORY "${frames}")
    message(STATUS "smoke: ${exe} --headless --mute --playtest-seconds 120")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env SUIKA_SHIPPED=1 "${exe}" --headless --mute --playtest-seconds 120
                --size 640x360 --playtest-render "${frames}"
        WORKING_DIRECTORY "${CPACK_TOPLEVEL_DIRECTORY}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 900)
    message(STATUS "smoke: exit ${rc}\n${out}${err}")
    if("${err}" MATCHES "Failed to create GLFW window" AND DEFINED ENV{GITHUB_ACTIONS})
        # GitHub's macOS runners have no OpenGL; the layout is checked above.
        message(WARNING "smoke: NOT RUN, no OpenGL on this runner; ${zip} layout checked only")
        continue()
    endif()
    if(NOT rc EQUAL 0 OR NOT out MATCHES "scripted playtest 120.0s" OR "${out}${err}" MATCHES "ERROR"
       OR NOT EXISTS "${frames}/t0060.png")
        message(FATAL_ERROR "smoke: ${zip} failed")
    endif()
endforeach()
