# Windows programs built on Linux or macOS with MinGW-w64 (x86_64-w64-mingw32-gcc),
# run under Wine: the windows-* presets.
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
