# wgrender's build: the library, its examples and its tests, on Windows (MSVC in
# Visual Studio, or MinGW), Linux, macOS and the web (Emscripten).
#
#   cmake --preset linux-release && cmake --build --preset linux-release   (CMakePresets.json)
#   ctest --preset linux-headless                                          unit tests + smoke run
#
# or open this folder in Visual Studio or VS Code, which read the presets. What a build
# makes is in out/<platform>/<variant>/, from the preset <platform>-<variant> (the wg*
# family's layout: linux/release, windows/msvc, web/webgl2-nothreads, ...): the library
# at the top, the programs beside it, and for the web the site. CMake's own work (its
# cache, the objects) is in build/<platform>/<variant>/. A program of your own:
#
#   add_subdirectory(wgrender-c)
#   target_link_libraries(my_game PRIVATE wgrender)
#
# which brings wgrender's public headers and the platform libraries it needs (OpenGL,
# X11, ALSA, the Windows and macOS frameworks) with it.
#
# What is built, and with what, is build.json: the sources, and per target the
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
# Under Emscripten (the web presets, or emcmake):
#   WGR_WEB_BACKEND webgl2 | webgpu     WGR_WEB_THREADS ON | OFF     WGR_WEB_DEBUG OFF | ON

cmake_minimum_required(VERSION 3.21) # string(JSON), and C_STANDARD 11 on MSVC

# The web presets set WGR_WEB: Emscripten's toolchain, from $EMSDK or else the emcc on
# PATH (as emsdk_env or an installed Emscripten puts it), so no preset names a path.
if(WGR_WEB AND NOT CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{EMSDK})
    set(emscripten "$ENV{EMSDK}/upstream/emscripten")
  else()
    find_program(WGR_EMCC NAMES emcc emcc.bat)
    if(NOT WGR_EMCC)
      message(FATAL_ERROR "wgrender: a web preset needs Emscripten: no EMSDK, and no emcc on PATH")
    endif()
    file(REAL_PATH "${WGR_EMCC}" emcc)
    get_filename_component(emscripten "${emcc}" DIRECTORY)
  endif()
  set(CMAKE_TOOLCHAIN_FILE "${emscripten}/cmake/Modules/Platform/Emscripten.cmake")
endif()

project(wgrender LANGUAGES C)

# A gcc on PATH may come without its binutils beside it: choosenim's MinGW puts only a
# gcc.exe shim in .nimble/bin. CMake then finds no ar and fails at the first archive;
# the compiler knows where its own are, so ask it.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  foreach(tool ar ranlib)
    string(TOUPPER "${tool}" var)
    if(NOT CMAKE_${var} OR CMAKE_${var} MATCHES "NOTFOUND")
      execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-prog-name=${tool}
                      OUTPUT_VARIABLE found OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(IS_ABSOLUTE "${found}" AND EXISTS "${found}")
        # both: the compiler's generated settings define it as a normal variable too,
        # which would hide the cache entry
        set(CMAKE_${var} "${found}" CACHE FILEPATH "${tool}, from ${CMAKE_C_COMPILER}" FORCE)
        set(CMAKE_${var} "${found}")
      endif()
    endif()
  endforeach()
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/build.json" WGR_MANIFEST)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS build.json)

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

# What a build makes goes to out/<platform>/<variant>/, and its work stays in
# build/<platform>/<variant>/ (CONVENTIONS.md, "Build directories"): the preset's build
# directory with build/ for out/. Only for a build directory under wgrender's own build/;
# a project that add_subdirectory()s wgrender keeps CMake's defaults. Every
# configuration's too, or a multi-config generator (Visual Studio) adds Release/.
file(RELATIVE_PATH WGR_BUILD_REL "${WGR_ROOT}/build" "${CMAKE_BINARY_DIR}")
if(PROJECT_IS_TOP_LEVEL AND NOT IS_ABSOLUTE "${WGR_BUILD_REL}" AND NOT WGR_BUILD_REL MATCHES "^\\.\\.")
  set(WGR_OUT "${WGR_ROOT}/out/${WGR_BUILD_REL}")
else()
  set(WGR_OUT "${CMAKE_BINARY_DIR}")
endif()
foreach(kind ARCHIVE LIBRARY RUNTIME)
  set(CMAKE_${kind}_OUTPUT_DIRECTORY "${WGR_OUT}")
  foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_${config} "${WGR_OUT}")
  endforeach()
endforeach()
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
    # __FILE__ (the log's source locations) relative to the repository, as make's and
    # buildweb's relative paths had it: shorter strings, and no build machine's paths
    target_compile_options(${target} PRIVATE ${WGR_WARN} "-fmacro-prefix-map=${WGR_ROOT}/=")
  endif()
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 12)
    # False positives gcc 12 fixed: a field a designated initializer leaves out
    # (.depth.load_action = ..., Clay's CLAY() declarations) counted as missing, and an
    # array parameter (const float weights[4]) read as a region of 4 bytes
    target_compile_options(${target} PRIVATE -Wno-missing-field-initializers -Wno-stringop-overread)
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
  wgr_manifest_list(ldflags web ${WGR_TARGET} ldflags)
  # CMake de-duplicates options, so a flag's separate value (--closure 1) goes with it
  set(WGR_LDFLAGS)
  foreach(flag IN LISTS ldflags)
    if(NOT flag MATCHES "^-" AND WGR_LDFLAGS)
      list(POP_BACK WGR_LDFLAGS previous)
      set(flag "SHELL:${previous} ${flag}")
    endif()
    list(APPEND WGR_LDFLAGS "${flag}")
  endforeach()
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
  if(WGR_TARGET STREQUAL "linux" AND Python3_FOUND)
    # the system's GL, X11 and ALSA dev packages: stop here, with the command that
    # installs them, rather than at the link
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${WGR_ROOT}/tools/deps.py" check
                    RESULT_VARIABLE deps_missing)
    if(deps_missing)
      message(FATAL_ERROR "wgrender: missing system build dependencies (above)")
    endif()
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
    if(EMSCRIPTEN)
      target_compile_options(${name} PRIVATE ${WGR_CFLAGS}) # as the library: NDEBUG, backend
    endif()
    list(APPEND WGR_EXAMPLE_TARGETS ${name})
  endforeach()

  # On the web, the build directory is the site: <example>.js and .wasm, and the page
  # and examples.json that tools/webdeploy.py writes once they're linked. Serve it with
  # tools/serve.py; tools/site.py makes a copy to hand a static host.
  if(EMSCRIPTEN AND Python3_FOUND)
    add_custom_target(webdeploy ALL
      COMMAND "${Python3_EXECUTABLE}" "${WGR_ROOT}/tools/webdeploy.py" "${WGR_OUT}"
              "${WGR_ROOT}/examples/web/index.html"
      DEPENDS ${WGR_EXAMPLE_TARGETS} "${WGR_ROOT}/examples/web/index.html"
      COMMENT "webdeploy: index.html and examples.json")
  endif()
endif()

# The rest is for working on wgrender itself, so only when it's the top-level project.
if(PROJECT_IS_TOP_LEVEL)
  # A program of wgrender's own, compiled as the examples are, plus the backend defines
  # for the ones that reach into sokol (spritebench)
  function(wgr_program name)
    add_executable(${name} EXCLUDE_FROM_ALL ${ARGN})
    wgr_c_target(${name})
    target_include_directories(${name} PRIVATE ${WGR_INCLUDE})
    target_include_directories(${name} SYSTEM PRIVATE ${WGR_SYSTEM_INCLUDE})
    target_link_libraries(${name} PRIVATE wgrender)
    if(EMSCRIPTEN)
      target_compile_options(${name} PRIVATE ${WGR_CFLAGS})
    else()
      target_compile_definitions(${name} PRIVATE ${WGR_DEFINES})
    endif()
  endfunction()

  # The benchmarks (tools/bench; tools/bench/run.py builds and runs one). On the web
  # they are pages of their own under bench/, out of the examples list and webcheck:
  # serve the site, then open /bench/?ex=spritebench (results in the browser console).
  # WGR_LOADBENCH_FLAGS picks what loadbench loads, e.g.
  # "-DLOADBENCH_HELMET_ONLY;-DLOADBENCH_KTX=1".
  set(WGR_LOADBENCH_FLAGS "" CACHE STRING "compile flags for loadbench")
  foreach(name loadbench spritebench shadowbench stress)
    wgr_program(${name} "${WGR_ROOT}/tools/bench/${name}.c")
    list(APPEND WGR_BENCH_TARGETS ${name})
    if(EMSCRIPTEN)
      set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${WGR_OUT}/bench")
      add_custom_command(TARGET ${name} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${WGR_ROOT}/tools/webdeploy.py" "${WGR_OUT}/bench"
                "${WGR_ROOT}/examples/web/index.html")
    endif()
  endforeach()
  target_compile_options(loadbench PRIVATE ${WGR_LOADBENCH_FLAGS})
  add_custom_target(benches DEPENDS ${WGR_BENCH_TARGETS})

  # Generated files, committed; rebuild them after changing what they come from.
  #   gen-brdf-lut          src/data/wgr_brdf_lut.h, baked by the library's own function
  #   gen-shaders           src/shaders/*.glsl.h (tools/gen_shaders.py: sokol-shdc)
  #   gen-example-shaders   examples/assets/shaders/*.wgrshader (tools/shaderpack.py)
  if(NOT EMSCRIPTEN AND NOT CMAKE_CROSSCOMPILING)
    wgr_program(gen_brdf_lut "${WGR_ROOT}/tools/gen_brdf_lut.c")
    add_custom_target(gen-brdf-lut COMMAND gen_brdf_lut "${WGR_ROOT}/src/data/wgr_brdf_lut.h"
                      COMMENT "gen-brdf-lut: src/data/wgr_brdf_lut.h")
  endif()
  if(Python3_FOUND)
    add_custom_target(gen-shaders COMMAND "${Python3_EXECUTABLE}" "${WGR_ROOT}/tools/gen_shaders.py"
                      WORKING_DIRECTORY "${WGR_ROOT}" USES_TERMINAL)
    add_custom_target(gen-example-shaders COMMAND "${Python3_EXECUTABLE}" "${WGR_ROOT}/tools/gen_shaders.py" --examples
                      WORKING_DIRECTORY "${WGR_ROOT}" USES_TERMINAL)
  endif()
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
  # The target's name, not its file, so that ctest runs it under the emulator (Wine) in
  # a cross build. ThreadSanitizer needs ASLR off on Linux, which is what setarch -R does.
  set(unit_command unit_tests)
  if(WGR_SANITIZE STREQUAL "thread" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(unit_command setarch ${CMAKE_SYSTEM_PROCESSOR} -R $<TARGET_FILE:unit_tests>)
  endif()
  # tests/: the tests load assets as ../examples/assets
  add_test(NAME unit COMMAND ${unit_command} WORKING_DIRECTORY "${WGR_ROOT}/tests")

  if(Python3_FOUND AND NOT CMAKE_CROSSCOMPILING) # the modules check reads the library with the host's nm
    add_test(NAME check COMMAND "${Python3_EXECUTABLE}" "${WGR_ROOT}/tools/check.py"
                                --lib $<TARGET_FILE:wgrender>)
  endif()

  # Every example, headless for 180 frames (3 s at the paced 60 fps): fails on a bad
  # exit, a timeout, or an error in its log (tools/smoke.py). Python by its path, not
  # the imported Python3::Interpreter, which a cross build would run under the emulator.
  if(WGR_EXAMPLES AND Python3_FOUND)
    set(runner_arg)
    if(WGR_RUNNER)
      list(JOIN WGR_RUNNER " " runner)
      set(runner_arg "--runner=${runner}")
    endif()
    foreach(name IN LISTS WGR_EXAMPLE_TARGETS)
      add_test(NAME smoke.${name}
               COMMAND "${Python3_EXECUTABLE}" "${WGR_ROOT}/tools/smoke.py" --frames 180
                       ${runner_arg} $<TARGET_FILE:${name}>
               WORKING_DIRECTORY "${WGR_ROOT}")
    endforeach()
  endif()
endif()
