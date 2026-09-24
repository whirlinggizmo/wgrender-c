# wgrender's build: the library, its examples and its tests, on Windows (MSVC in
# Visual Studio, or MinGW), Linux, macOS and the web (Emscripten).
#
#   cmake --preset desktop && cmake --build --preset desktop     (CMakePresets.json)
#   ctest --preset headless                                      unit tests + smoke run
#
# or open this folder in Visual Studio or VS Code, which read the presets. Everything a
# build makes is under build/<preset>/. A program of your own:
#
#   add_subdirectory(wgrender-c)
#   target_link_libraries(my_game PRIVATE wgrender)
#
# which brings wgrender's public headers and the platform libraries it needs (OpenGL,
# X11, ALSA, the Windows and macOS frameworks) with it.
#
# What is built, and with what, is mk/build.json: the sources, and per target the
# defines, flags and libraries. The bindings read the same file to build wgrender with
# their own toolchains (wgrender-nim's build.nim, tools/buildweb.py), so this file
# lists no source, flag or library of its own.
#
# Options:
#   WGR_HEADLESS   no window, GPU or audio device (sokol's dummy backend)
#   WGR_EXAMPLES   examples/*.c (run them from this directory: they load examples/assets
#                  relative to the working directory)
#   WGR_TESTS      tests/unit, and a smoke test per example (headless builds)
#   WGR_SANITIZE   thread | address | undefined: the library and tests built with it
# Under Emscripten (emcmake, or the web presets):
#   WGR_WEB_BACKEND webgl2 | webgpu     WGR_WEB_THREADS ON | OFF     WGR_WEB_DEBUG OFF | ON

cmake_minimum_required(VERSION 3.21) # string(JSON), and C_STANDARD 11 on MSVC
project(wgrender LANGUAGES C)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mk/build.json" WGR_MANIFEST)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS mk/build.json)

# A JSON array in the manifest, as a CMake list: wgr_manifest_list(out key [key ...])
function(wgr_manifest_list out)
  string(JSON length ERROR_VARIABLE err LENGTH "${WGR_MANIFEST}" ${ARGN})
  if(err)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  set(items)
  if(length GREATER 0)
    math(EXPR last "${length} - 1")
    foreach(i RANGE ${last})
      string(JSON item GET "${WGR_MANIFEST}" ${ARGN} ${i})
      list(APPEND items "${item}")
    endforeach()
  endif()
  set(${out} "${items}" PARENT_SCOPE)
endfunction()

option(WGR_HEADLESS "No window, GPU or audio device (sokol's dummy backend)" OFF)
option(WGR_EXAMPLES "Build examples/*.c" OFF)
option(WGR_TESTS "Unit tests and a smoke test per example (headless builds)" OFF)
set(WGR_SANITIZE "" CACHE STRING "thread, address or undefined: the library and tests built with it")

find_package(Python3 COMPONENTS Interpreter) # the smoke tests' runner

set(WGR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
wgr_manifest_list(WGR_SOURCES sources)
list(TRANSFORM WGR_SOURCES PREPEND "${WGR_ROOT}/")
wgr_manifest_list(WGR_INCLUDE include)
wgr_manifest_list(WGR_SYSTEM_INCLUDE system_include)
list(TRANSFORM WGR_INCLUDE PREPEND "${WGR_ROOT}/")
list(TRANSFORM WGR_SYSTEM_INCLUDE PREPEND "${WGR_ROOT}/")
wgr_manifest_list(WGR_WARN warn)
wgr_manifest_list(WGR_OPT opt)

# Our code, compiled as wgrender compiles: gnu11 where there is one, the manifest's
# warnings where the compiler speaks gcc's dialect, the sanitizer when asked for. With
# no build type (the presets but debug), the manifest's optimization and no NDEBUG:
# wgrender's desktop builds keep their asserts, where CMake's Release would drop them.
# A sanitizer build is -O1 -g, which keeps its reports readable.
function(wgr_c_target target)
  set_target_properties(${target} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE ${WGR_WARN})
  endif()
  if(WGR_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=${WGR_SANITIZE} -fno-omit-frame-pointer -O1 -g)
    target_link_options(${target} PRIVATE -fsanitize=${WGR_SANITIZE})
  elseif(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES AND NOT EMSCRIPTEN)
    target_compile_options(${target} PRIVATE ${WGR_OPT})
  endif()
endfunction()

add_library(wgrender STATIC ${WGR_SOURCES})
wgr_c_target(wgrender)
target_include_directories(wgrender PUBLIC "${WGR_ROOT}/include")
target_include_directories(wgrender PRIVATE ${WGR_INCLUDE})
target_include_directories(wgrender SYSTEM PRIVATE ${WGR_SYSTEM_INCLUDE})

if(EMSCRIPTEN)
  set(WGR_WEB_BACKEND webgl2 CACHE STRING "webgl2 or webgpu")
  option(WGR_WEB_THREADS "Asset decoding on worker threads (needs COOP/COEP headers)" ON)
  option(WGR_WEB_DEBUG "No optimization, debug info and assertions" OFF)
  set(WGR_TARGET "${WGR_WEB_BACKEND}")
  if(NOT WGR_WEB_THREADS)
    string(APPEND WGR_TARGET "-nothreads")
  endif()
  if(WGR_WEB_DEBUG)
    string(APPEND WGR_TARGET "-debug")
  endif()
  wgr_manifest_list(WGR_CFLAGS web ${WGR_TARGET} cflags)
  wgr_manifest_list(WGR_PROGRAM_CFLAGS web ${WGR_TARGET} program_cflags)
  wgr_manifest_list(WGR_LDFLAGS web ${WGR_TARGET} ldflags)
  target_compile_options(wgrender PRIVATE ${WGR_CFLAGS})
  target_compile_options(wgrender INTERFACE ${WGR_PROGRAM_CFLAGS})
  target_link_options(wgrender INTERFACE ${WGR_LDFLAGS})
else()
  if(WIN32)
    set(WGR_TARGET windows)
  elseif(APPLE)
    set(WGR_TARGET macos)
  else()
    set(WGR_TARGET linux)
  endif()
  if(WGR_HEADLESS)
    string(APPEND WGR_TARGET "-headless")
  endif()
  wgr_manifest_list(WGR_DEFINES desktop ${WGR_TARGET} defines)
  wgr_manifest_list(WGR_LIBS desktop ${WGR_TARGET} libs)
  wgr_manifest_list(WGR_FRAMEWORKS desktop ${WGR_TARGET} frameworks)
  target_compile_definitions(wgrender PRIVATE ${WGR_DEFINES})
  target_link_libraries(wgrender INTERFACE ${WGR_LIBS})
  foreach(framework IN LISTS WGR_FRAMEWORKS)
    target_link_libraries(wgrender INTERFACE "-framework ${framework}")
  endforeach()
  string(JSON WGR_STATIC ERROR_VARIABLE err GET "${WGR_MANIFEST}" desktop ${WGR_TARGET} static)
  if(MINGW AND WGR_STATIC)
    target_link_options(wgrender INTERFACE -static) # MinGW's runtime, statically
  endif()
endif()
message(STATUS "wgrender: ${WGR_TARGET}")

# How a program here runs: directly, or under CMAKE_CROSSCOMPILING_EMULATOR (Wine for
# a Windows build made on Linux: tools/wine.py)
set(WGR_RUNNER ${CMAKE_CROSSCOMPILING_EMULATOR})

if(WGR_EXAMPLES)
  file(GLOB WGR_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${WGR_ROOT}/examples/*.c")
  foreach(source IN LISTS WGR_EXAMPLE_SOURCES)
    get_filename_component(name "${source}" NAME_WE)
    add_executable(${name} "${source}")
    wgr_c_target(${name})
    # the examples use wgrender's internals and vendored headers too (clay.c: deps/clay)
    target_include_directories(${name} PRIVATE ${WGR_INCLUDE})
    target_include_directories(${name} SYSTEM PRIVATE ${WGR_SYSTEM_INCLUDE} "${WGR_ROOT}/deps/clay")
    target_link_libraries(${name} PRIVATE wgrender)
    list(APPEND WGR_EXAMPLE_TARGETS ${name})
  endforeach()
endif()

if(WGR_TESTS)
  if(NOT WGR_HEADLESS)
    message(FATAL_ERROR "WGR_TESTS needs WGR_HEADLESS: the tests run with no window, GPU or audio")
  endif()
  enable_testing()
  file(GLOB WGR_UNIT_SOURCES CONFIGURE_DEPENDS "${WGR_ROOT}/tests/unit/*.c")
  add_executable(unit_tests ${WGR_UNIT_SOURCES})
  wgr_c_target(unit_tests)
  target_include_directories(unit_tests PRIVATE ${WGR_INCLUDE} "${WGR_ROOT}/tests/unit")
  target_include_directories(unit_tests SYSTEM PRIVATE ${WGR_SYSTEM_INCLUDE})
  target_compile_definitions(unit_tests PRIVATE ${WGR_DEFINES})
  target_link_libraries(unit_tests PRIVATE wgrender)
  # ThreadSanitizer needs ASLR off on Linux, which is what setarch -R does
  set(unit_command $<TARGET_FILE:unit_tests>)
  if(WGR_SANITIZE STREQUAL "thread" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(unit_command setarch ${CMAKE_SYSTEM_PROCESSOR} -R $<TARGET_FILE:unit_tests>)
  endif()
  # tests/: the tests load assets as ../examples/assets
  add_test(NAME unit COMMAND ${unit_command} WORKING_DIRECTORY "${WGR_ROOT}/tests")

  if(Python3_FOUND)
    add_test(NAME check COMMAND Python3::Interpreter "${WGR_ROOT}/tools/check.py"
                                --lib $<TARGET_FILE:wgrender>)
  endif()

  # Every example, headless for 180 frames (3 s at the paced 60 fps): fails on a bad
  # exit, a timeout, or an error in its log (tools/smoke.py)
  if(WGR_EXAMPLES AND Python3_FOUND)
    set(runner_arg)
    if(WGR_RUNNER)
      list(JOIN WGR_RUNNER " " runner)
      set(runner_arg "--runner=${runner}")
    endif()
    foreach(name IN LISTS WGR_EXAMPLE_TARGETS)
      add_test(NAME smoke.${name}
               COMMAND Python3::Interpreter "${WGR_ROOT}/tools/smoke.py" --frames 180
                       ${runner_arg} $<TARGET_FILE:${name}>
               WORKING_DIRECTORY "${WGR_ROOT}")
    endforeach()
  endif()
endif()
