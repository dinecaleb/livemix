#!/usr/bin/env bash
# Usage: scripts/build.sh [Release|Debug] [target]
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
CONFIG="${1:-Release}"
TARGET="${2:-all}"
BUILD_DIR="build"
[ "$CONFIG" = "Debug" ] && BUILD_DIR="build-debug"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" --target "$TARGET"
