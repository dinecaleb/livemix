#!/usr/bin/env bash
# Package DLIVE.app as a zip another Mac can run, for testing.
#
#   scripts/package.sh                 build (Release) and package for this Mac's architecture
#   scripts/package.sh --universal     arm64 + x86_64, so an Intel Mac can run it too (slow: JUCE builds twice)
#   scripts/package.sh --no-build      package whatever is already built
#   scripts/package.sh --out <dir>     where the zip goes (default: ~/Documents/dliveApp)
#
# There is no Developer ID certificate on this machine, so the app is signed
# ad-hoc: it is a complete, valid signature (the Info.plist is bound and the
# resources are sealed), it is simply not one Apple has vouched for. That is the
# difference between macOS saying "unverified developer - open it anyway?" and
# macOS saying "damaged - move to Trash", and the second one is what an
# unsigned or linker-only-signed bundle gets. The tester still has to let it
# through once; NOTES.txt beside the zip tells them how in two lines.
#
# For a build anyone can double-click with no warning at all, the app needs a
# "Developer ID Application" certificate and a trip through notarytool. That is
# a real distribution step, not a testing one.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"          # cmake / ninja from uv tool

BUILD=1
UNIVERSAL=0
OUT_DIR="$HOME/Documents/dliveApp"

while [ $# -gt 0 ]; do
    case "$1" in
        --universal) UNIVERSAL=1 ;;
        --no-build)  BUILD=0 ;;
        --out)       shift; OUT_DIR="${1:?--out needs a directory}" ;;
        -h|--help)   sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

BUILD_DIR=build
APP="$BUILD_DIR/app/DLive_artefacts/Release/DLIVE.app"

if [ "$UNIVERSAL" = "1" ]; then
    # A separate build directory: flipping the architectures in place would make every
    # other target in build/ stale and trigger a full rebuild the next time anything else
    # is touched.
    BUILD_DIR=build-universal
    APP="$BUILD_DIR/app/DLive_artefacts/Release/DLIVE.app"
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIVEMIX_UNIVERSAL_BINARY=ON -DLIVEMIX_BUILD_TESTS=OFF
elif [ "$BUILD" = "1" ]; then
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
fi

[ "$BUILD" = "1" ] && cmake --build "$BUILD_DIR" --target DLive

[ -d "$APP" ] || { echo "no app at $APP - build it first" >&2; exit 1; }

VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP/Contents/Info.plist" 2>/dev/null || echo 0.0.0)
ARCHS=$(lipo -archs "$APP/Contents/MacOS/DLIVE")

# A complete ad-hoc signature over the whole bundle, replacing the partial one the linker
# leaves behind. --force because the linker already put one there.
codesign --force --deep --sign - --identifier com.dine.dlive "$APP"
codesign --verify --deep --strict "$APP"

mkdir -p "$OUT_DIR"
STAMP=$(date +%Y%m%d)
ZIP="$OUT_DIR/DLIVE-$VERSION-$STAMP.zip"
rm -f "$ZIP"

# ditto, not zip: an .app is full of symlinks and a plain zip flattens them, which is
# itself a way to arrive as "damaged".
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"

cat > "$OUT_DIR/NOTES.txt" <<'NOTES'
DLIVE - test build

1. Unzip, drag DLIVE.app to Applications.
2. The FIRST time only: right-click (or Control-click) the app and choose Open,
   then Open again in the dialog. Double-clicking it instead will say Apple
   cannot check it for malicious software, and will not offer a way through.

   If macOS says the app is damaged, it is not - that is the same block worded
   differently. Run this once in Terminal and open it normally:

       xattr -dr com.apple.quarantine /Applications/DLIVE.app

3. It will ask for microphone access the first time it opens an audio device.
   That is DLIVE reading the inputs of your interface or console; say yes or it
   has nothing to mix. System Settings > Privacy & Security > Microphone if it
   is ever refused by accident.

Needs macOS 11 or later. Sessions are written to ~/Music/DLIVE/.
No console? File > Import Multitrack Folder... turns a folder of stems into a
session you can mix, tune and export.
NOTES

echo
echo "  $ZIP"
echo "  $(du -h "$ZIP" | cut -f1)  $ARCHS  version $VERSION"
echo "  notes: $OUT_DIR/NOTES.txt"
