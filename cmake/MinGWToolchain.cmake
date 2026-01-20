# Name of the target system
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Toolchain prefix (standard for Ubuntu/Debian mingw-w64 packages)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

# Cross-compilers
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

# Target environment root (where to find headers/libs for the target)
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

# Adjust behavior of find_xxx commands
# Search headers/libs in the target environment, programs in the host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static linking preferences
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")