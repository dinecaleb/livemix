#!/usr/bin/env bash
# Builds and runs the unit/integration tests, then the performance benchmark.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
BUILD_DIR="${BUILD_DIR:-build}"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target livemix_tests livemix_benchmark
ctest --test-dir "$BUILD_DIR" --output-on-failure --timeout 180
"$BUILD_DIR/tests/livemix_benchmark"
