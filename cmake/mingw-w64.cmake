# Windows programs built with MinGW-w64: the windows-mingw* presets. On Linux or macOS
# that's a cross build (x86_64-w64-mingw32-gcc), run under Wine; on Windows it's the gcc
# on PATH (MSYS2's, or the one choosenim installs for Nim), run as it is.
if(CMAKE_HOST_WIN32)
  # a native build: naming the system would make CMake treat it as a cross build
  set(CMAKE_C_COMPILER gcc)
else()
  set(CMAKE_SYSTEM_NAME Windows)
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
  set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
  set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

  # ctest runs the tests and examples under Wine (tools/wine.py runs by its #! line: the
  # host here is Linux or macOS)
  set(CMAKE_CROSSCOMPILING_EMULATOR "${CMAKE_CURRENT_LIST_DIR}/../tools/wine.py")
endif()
