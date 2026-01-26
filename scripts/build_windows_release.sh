#!/bin/bash
set -e

# 1. Directory Checks
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: Please run this script from the project root directory."
    exit 1
fi

# 2. Check for MinGW
if ! command -v x86_64-w64-mingw32-g++ &> /dev/null; then
    echo "Error: MinGW compiler not found."
    echo "On Debian/Ubuntu: sudo apt-get install mingw-w64"
    exit 1
fi

echo ">>> Configuring for Windows (Cross-Compile)..."

# 3. Clean & Configure
rm -rf build/windows_release
mkdir -p build/windows_release

cmake -S . \
      -B build/windows_release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/MinGWToolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF

echo ">>> Building..."
cmake --build build/windows_release --config Release --parallel $(nproc)

# --- 4. NEW: Packaging Step ---
echo ">>> Packaging for Distribution..."

DIST_DIR="dist/windows_goose_engine"

# Clean previous dist
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# Copy Executable
if [ -f "build/windows_release/gooseVoxelEngine.exe" ]; then
    cp build/windows_release/gooseVoxelEngine.exe "$DIST_DIR/"
else
    echo "Error: Executable not found. Build likely failed."
    exit 1
fi

# Strip Debug Symbols 
# (This removes symbol tables, often reducing .exe size by 50-80% for C++ releases)
if command -v x86_64-w64-mingw32-strip &> /dev/null; then
    echo "   Stripping debug symbols..."
    x86_64-w64-mingw32-strip --strip-all "$DIST_DIR/gooseVoxelEngine.exe"
else
    echo "   Warning: 'strip' tool not found. Executable will be larger than necessary."
fi

# Copy Resources (Check if exists first)
if [ -d "resources" ]; then
    echo "   Copying resources..."
    cp -r resources "$DIST_DIR/"
fi

echo ">>> Success!"
echo "    Final Package Location: $DIST_DIR"
ls -lh "$DIST_DIR"
