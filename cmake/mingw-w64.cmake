# Windows programs built with MinGW-w64: the windows-x64-mingw-* presets. On Linux or
# macOS that's a cross build (x86_64-w64-mingw32-gcc from the system's packages), run
# under Wine; on Windows it's the pinned WinLibs GCC that tools/setup_mingw.py sets up in
# the per-user cache the first time, never whichever gcc is on PATH, run as it is.
if(CMAKE_HOST_WIN32)
  # Once per configure; CMake reads a toolchain file again for each try_compile, which
  # gets the answer from this cache entry or, in a try_compile's own project, asks the
  # script again (which only prints once the compiler is there).
  if(NOT WGR_MINGW_BIN)
    find_program(WGR_MINGW_PYTHON NAMES python3 python py REQUIRED)
    execute_process(COMMAND "${WGR_MINGW_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/setup_mingw.py"
                    OUTPUT_VARIABLE bin OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE failed)
    if(failed OR NOT EXISTS "${bin}/gcc.exe")
      message(FATAL_ERROR "wgrender: setting up MinGW-w64 failed (tools/setup_mingw.py, above)")
    endif()
    file(TO_CMAKE_PATH "${bin}" bin)
    set(WGR_MINGW_BIN "${bin}" CACHE INTERNAL "the pinned MinGW-w64's bin (tools/setup_mingw.py)")
  endif()
  # a native build: naming the system would make CMake treat it as a cross build
  set(CMAKE_C_COMPILER "${WGR_MINGW_BIN}/gcc.exe")
  set(CMAKE_RC_COMPILER "${WGR_MINGW_BIN}/windres.exe")
else()
  set(CMAKE_SYSTEM_NAME Windows)
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
  set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
  set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

  # ctest runs the tests and examples under Wine (tools/run_windows_program.py runs by its #! line: the
  # host here is Linux or macOS)
  set(CMAKE_CROSSCOMPILING_EMULATOR "${CMAKE_CURRENT_LIST_DIR}/../tools/run_windows_program.py")
endif()
