# toolchain-mingw64.cmake
# Cross-compile to Windows from Linux using MinGW-w64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compilers
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Root path for MinGW sysroot
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

# Only search inside the toolchain sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Make WIN32 behave in CMake
set(WIN32 TRUE)

# Enable POSIX-compatible printf in MinGW-w64 so that %zu is accepted
# by -Wformat.
set(CMAKE_C_FLAGS_INIT "-D__USE_MINGW_ANSI_STDIO=1")
set(CMAKE_CXX_FLAGS_INIT "-D__USE_MINGW_ANSI_STDIO=1")
