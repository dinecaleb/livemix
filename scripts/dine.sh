#!/usr/bin/env bash
# Build DINE and run it.
#
#   scripts/dine.sh              build the app (Release) and open it
#   scripts/dine.sh --build      build only, do not open
#   scripts/dine.sh --debug      the same, from build-debug
#   scripts/dine.sh --tests      build and run the app tests and the engine tests
#   scripts/dine.sh --shots [dir]  build and render the UI snapshots (default: build/app-snapshots)
#
# Only the app is built, not the plug-in products: this is the fast loop.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"          # cmake / ninja from uv tool

CONFIG=Release
BUILD_DIR=build
MODE=run
SHOT_DIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --debug)  CONFIG=Debug; BUILD_DIR=build-debug ;;
        --build)  MODE=build ;;
        --tests)  MODE=tests ;;
        --shots)  MODE=shots; [ $# -gt 1 ] && { SHOT_DIR="$2"; shift; } ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

case "$MODE" in
    tests) TARGETS=(dine_app_tests livemix_tests) ;;
    shots) TARGETS=(dine_ui_snapshots) ;;
    *)     TARGETS=(DineApp) ;;
esac

cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG" > /dev/null
cmake --build "$BUILD_DIR" --target "${TARGETS[@]}"

APP="$BUILD_DIR/app/DineApp_artefacts/$CONFIG/DINE.app"

case "$MODE" in
    run)
        echo "opening $APP"
        open "$APP"
        ;;
    tests)
        "$BUILD_DIR/app/dine_app_tests"
        "$BUILD_DIR/tests/livemix_tests"
        ;;
    shots)
        DIR="${SHOT_DIR:-$BUILD_DIR/app-snapshots}"
        mkdir -p "$DIR"
        "$BUILD_DIR/app/dine_ui_snapshots" "$DIR"
        echo "snapshots in $DIR"
        ;;
esac
