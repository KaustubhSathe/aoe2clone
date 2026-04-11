#!/bin/bash
set -e

EMSDK="/d/Code/emsdk"
export PATH="$EMSDK:$EMSDK/upstream/emscripten:$PATH"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_web"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

emcmake cmake "$SCRIPT_DIR/web" \
    -DCMAKE_BUILD_TYPE=Release \
    -G "MinGW Makefiles" \
    -DCMAKE_MAKE_PROGRAM=mingw32-make

emmake mingw32-make -j4

echo ""
echo "Build complete! Output in build_web/"
echo "  aoe2.html - open in browser"
echo ""
echo "To serve locally:"
echo "  cd build_web && python -m http.server 8080"
echo "  Then open http://localhost:8080/aoe2.html"
