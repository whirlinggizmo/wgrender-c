# wgrender for CMake: the library, for a C or C++ program that builds with CMake, and
# the examples, on Windows with Visual Studio (MSVC), MinGW, Linux and macOS.
#
#   cmake -B build/cmake -DWGR_EXAMPLES=ON && cmake --build build/cmake --config Release
#
# or open this folder in Visual Studio or VS Code. A program of your own:
#
#   add_subdirectory(wgrender-c)
#   target_link_libraries(my_game PRIVATE wgrender)
#
# which brings wgrender's public headers and the platform libraries it needs (OpenGL,
# X11, ALSA, the Windows and macOS frameworks) with it.
#
# The Makefile is still the one description of the build: this reads it, as
# mk/build.json (tools/gen_manifest.py writes that from the Makefiles, and make check
# fails when it is stale), for the sources, and per target the defines, flags and
# libraries. So nothing here lists a source or a library, and nothing here can drift.
#
# Options:
#   WGR_HEADLESS   no window, GPU or audio device (sokol's dummy backend): tests, CI
#   WGR_EXAMPLES   build examples/*.c too (run them from this directory: they load
#                  examples/assets relative to the working directory)
# Under emcmake (Emscripten), the web library instead, as tools/buildweb.py builds it:
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

wgr_manifest_list(WGR_SOURCES sources)
list(TRANSFORM WGR_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
add_library(wgrender STATIC ${WGR_SOURCES})
set_target_properties(wgrender PROPERTIES C_STANDARD 11 C_EXTENSIONS ON) # gnu11 where there is one

wgr_manifest_list(WGR_INCLUDE include)
wgr_manifest_list(WGR_SYSTEM_INCLUDE system_include)
list(TRANSFORM WGR_INCLUDE PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM WGR_SYSTEM_INCLUDE PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
target_include_directories(wgrender PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(wgrender PRIVATE ${WGR_INCLUDE})
target_include_directories(wgrender SYSTEM PRIVATE ${WGR_SYSTEM_INCLUDE})
if(NOT MSVC)
  wgr_manifest_list(WGR_WARN warn)
  target_compile_options(wgrender PRIVATE ${WGR_WARN})
endif()

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

if(WGR_EXAMPLES)
  file(GLOB WGR_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.c")
  foreach(source IN LISTS WGR_EXAMPLE_SOURCES)
    get_filename_component(name "${source}" NAME_WE)
    add_executable(${name} "${source}")
    set_target_properties(${name} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    # the examples use wgrender's internals and vendored headers too (clay.c: deps/clay)
    target_include_directories(${name} PRIVATE ${WGR_INCLUDE})
    target_include_directories(${name} SYSTEM PRIVATE ${WGR_SYSTEM_INCLUDE}
                               "${CMAKE_CURRENT_SOURCE_DIR}/deps/clay")
    target_link_libraries(${name} PRIVATE wgrender)
  endforeach()
endif()
