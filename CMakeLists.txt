cmake_minimum_required(VERSION 3.10)
project(GooseVoxelEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. FastNoise2 Submodule
add_subdirectory(external/FastNoise2)

# 2. Source Files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp" 
    "src/vendor/glad.c" 
    "src/vendor/imgui/*.cpp"
    "src/vendor/imgui/backends/imgui_impl_glfw.cpp"
    "src/vendor/imgui/backends/imgui_impl_opengl3.cpp"
)

# --- TARGET DEFINITION (Must happen before setting properties) ---
add_executable(gooseVoxelEngine ${SOURCES})

# 3. Optimizations (Applied after target creation)
# Global SIMD for all builds (FastNoise2 requirement)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|i386)")
    target_compile_options(gooseVoxelEngine PRIVATE -mavx2 -msse4.1)
endif()

# Release-Specific Optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Configuring for Release with MAX optimizations and Native SIMD...")
    
    # -O3: Aggressive optimization
    # -march=native: Utilizes ALL instructions on this CPU
    # -funroll-loops: Unrolls loops for speed
    target_compile_options(gooseVoxelEngine PRIVATE -O3 -march=native -funroll-loops)

    # Enable Link Time Optimization (LTO) if available
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT output)
    if(result)
        set_property(TARGET gooseVoxelEngine PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "Interprocedural Optimization (LTO) enabled")
    endif()
endif()

# 4. Include Directories
target_include_directories(gooseVoxelEngine PRIVATE 
    include
    src/vendor/imgui
    src/vendor/imgui/backends
)

# 5. Linking
target_link_libraries(gooseVoxelEngine PRIVATE 
    FastNoise2
    glfw
    GL
    dl
    pthread
    X11
)

# --- Post-Build ---
add_custom_command(TARGET gooseVoxelEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/resources
)