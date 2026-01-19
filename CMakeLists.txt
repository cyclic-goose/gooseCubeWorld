cmake_minimum_required(VERSION 3.10)
project(GooseVoxelEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable SIMD for FastNoise2 performance
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|i386)")
    add_compile_options(-mavx2 -msse4.1)
endif()

# 1. FastNoise2 Submodule
add_subdirectory(external/FastNoise2)

# 2. Source Files
# Explicitly ensuring src/vendor/glad.c is included
file(GLOB_RECURSE SOURCES 
    "src/*.cpp" 
    "src/vendor/glad.c" 
    "src/vendor/imgui/*.cpp"
    "src/vendor/imgui/backends/imgui_impl_glfw.cpp"
    "src/vendor/imgui/backends/imgui_impl_opengl3.cpp"
)

add_executable(gooseVoxelEngine ${SOURCES})

# 3. Include Directories
target_include_directories(gooseVoxelEngine PRIVATE 
    include             # This allows #include <glad/glad.h>
    src/vendor/imgui
    src/vendor/imgui/backends
)

# 4. Linking
# FastNoise2 target automatically handles its own include paths
target_link_libraries(gooseVoxelEngine PRIVATE 
    FastNoise2
    glfw
    GL
    dl
    pthread
    X11
)