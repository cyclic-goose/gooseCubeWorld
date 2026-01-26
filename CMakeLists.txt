cmake_minimum_required(VERSION 3.14)
project(GooseVoxelEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. Dependencies ---

include(FetchContent)

# 1.1 FastNoise2 (Submodule) - Check if exists first
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/FastNoise2/CMakeLists.txt")
    add_subdirectory(external/FastNoise2)
else()
    message(WARNING "FastNoise2 submodule not found. Make sure to run 'git submodule update --init --recursive'")
endif()

# 1.2 GLFW (Fetch from Web)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# Ensure we build static libs for Windows
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# 1.3 GLM (Fetch from Web)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
)
FetchContent_MakeAvailable(glm)

# 1.4 STB (Header-only)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# --- 2. Source Files ---
file(GLOB_RECURSE SOURCES 
    "src/*.cpp" 
    "src/vendor/glad.c" 
    "src/vendor/imgui/*.cpp"
    "src/vendor/imgui/backends/imgui_impl_glfw.cpp"
    "src/vendor/imgui/backends/imgui_impl_opengl3.cpp"
)

# --- 3. Target Definition ---
add_executable(gooseVoxelEngine ${SOURCES})

# --- 4. Include Directories ---
target_include_directories(gooseVoxelEngine PRIVATE 
    include
    src/vendor/imgui
    src/vendor/imgui/backends
    ${glfw_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
)

# --- 5. Optimizations ---

if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|i386)")
    # Basic vectorization for generic builds
    target_compile_options(gooseVoxelEngine PRIVATE -mavx2 -msse4.1)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CROSSCOMPILING)
        message(STATUS "Cross-compiling Release for Windows")
        # -s: Strip symbols (redundant if using strip tool, but good practice)
        # -static: Force static linking of gcc/stdc++
        target_compile_options(gooseVoxelEngine PRIVATE -O3 -mavx2 -funroll-loops)
        target_link_options(gooseVoxelEngine PRIVATE 
            -static 
            -static-libgcc 
            -static-libstdc++
            -s 
        )
    else()
        message(STATUS "Native Release: Using -march=native")
        target_compile_options(gooseVoxelEngine PRIVATE -O3 -march=native -funroll-loops)
    endif()

    # LTO (Link Time Optimization)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT output)
    if(result)
        set_property(TARGET gooseVoxelEngine PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# --- 6. Linking ---

target_link_libraries(gooseVoxelEngine PRIVATE 
    FastNoise2
    glfw 
    glm   
    ${GLFW_LIBRARIES}
)

if(WIN32)
    # Windows Link dependencies (Order matters for static MinGW linking!)
    target_link_libraries(gooseVoxelEngine PRIVATE 
        opengl32
        gdi32
        ws2_32
        dwmapi
        winmm
        shlwapi   # Often needed for static linking
        rpcrt4    # Often needed for static linking
        iphlpapi  # Network helpers
    )
    target_compile_definitions(gooseVoxelEngine PRIVATE _GLFW_WIN32)
else()
    # Linux Link dependencies
    target_link_libraries(gooseVoxelEngine PRIVATE 
        GL
        dl
        pthread
        X11
    )
endif()

# --- Post-Build ---
add_custom_command(TARGET gooseVoxelEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/resources
)