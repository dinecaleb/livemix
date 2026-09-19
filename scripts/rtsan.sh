#!/usr/bin/env bash
# Runs every test executable that was built with -DLIVEMIX_RTSAN=ON and fails if RealtimeSanitizer
# reports an unsafe call reached from a real-time entry point (LIVEMIX_NONBLOCKING, src/Core/Realtime.h).
#
#   scripts/rtsan.sh [build-dir]        default: build-rtsan
#
# Every report is printed (halt_on_error=false, so one run shows every violation, not the first)
# and a suppressions file next to this script, scripts/rtsan.supp, holds the violations that are
# documented rather than fixed - each with a comment saying why. Configure and build, for example:
#
#   LLVM=$(brew --prefix llvm@21)   # Apple clang has no -fsanitize=realtime; JUCE 8.0.8 needs clang <= 21
#   cmake -S . -B build-rtsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#         -DCMAKE_C_COMPILER=$LLVM/bin/clang -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
#         -DLIVEMIX_RTSAN=ON -DLIVEMIX_COPY_PLUGIN_AFTER_BUILD=OFF
#   cmake --build build-rtsan --target livemix_tests dlive_app_tests livemix_drums_plugin_tests ...
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build-rtsan}"
SUPP="$(pwd)/scripts/rtsan.supp"
export RTSAN_OPTIONS="halt_on_error=false:print_stats_on_exit=1:suppressions=$SUPP"

candidates=(
    "$BUILD_DIR/tests/livemix_tests"
    "$BUILD_DIR/app/dlive_app_tests"
    "$BUILD_DIR/modules/Drums/livemix_plugin_tests"
    "$BUILD_DIR/modules/Drums/livemix_drums_plugin_tests"
    "$BUILD_DIR/modules/Vocals/livemix_vocals_plugin_tests"
    "$BUILD_DIR/modules/Keys/livemix_keys_plugin_tests"
    "$BUILD_DIR/modules/Master/livemix_master_plugin_tests"
    "$BUILD_DIR/modules/Guitar/livemix_guitar_plugin_tests"
    "$BUILD_DIR/modules/Bass/livemix_bass_plugin_tests"
    "$BUILD_DIR/modules/FX/livemix_fx_plugin_tests"
)

ran=0
status=0
for exe in "${candidates[@]}"; do
    [ -x "$exe" ] || continue
    ran=$((ran + 1))
    log="$exe.rtsan.log"
    echo "=== $exe"
    "$exe" > "$log" 2>&1
    code=$?
    reports=$(grep -c "ERROR: RealtimeSanitizer" "$log" || true)
    if [ "$reports" -gt 0 ] || [ "$code" -ne 0 ]; then
        # Every distinct offender once, with where it was called from.
        grep -A 12 "ERROR: RealtimeSanitizer" "$log" | head -200
        echo "--- $exe: $reports RealtimeSanitizer report(s), exit $code (full log: $log)"
        status=1
    else
        tail -1 "$log"
        echo "--- $exe: clean"
    fi
done

if [ "$ran" -eq 0 ]; then
    echo "rtsan.sh: no test executables found under $BUILD_DIR - build them with -DLIVEMIX_RTSAN=ON first" >&2
    exit 2
fi
exit $status
