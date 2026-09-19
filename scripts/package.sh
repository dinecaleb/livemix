#!/usr/bin/env bash
# Package DLIVE.app as a zip another Mac can run, for testing - or, with a Developer ID, for anyone.
#
#   scripts/package.sh                 build (Release) and package for this Mac's architecture
#   scripts/package.sh --universal     arm64 + x86_64, so an Intel Mac can run it too (slow: JUCE builds twice)
#   scripts/package.sh --no-build      package whatever is already built
#   scripts/package.sh --out <dir>     where the zip goes (default: ~/Documents/dliveApp)
#
# Two ways to sign, chosen by the environment:
#
#   DEVELOPER_ID    unset: the app is signed AD-HOC. A complete, valid signature (the Info.plist is
#                   bound and the resources are sealed), just not one Apple has vouched for - the
#                   difference between "unverified developer - open it anyway?" and "damaged - move
#                   to Trash". The tester lets it through once; NOTES.txt tells them how.
#
#   DEVELOPER_ID    set: the "Developer ID Application: Name (TEAMID)" identity (its name or SHA-1,
#                   from `security find-identity -v -p codesigning`). The app is signed with it,
#                   with the hardened runtime, a secure timestamp and scripts/DLIVE.entitlements
#                   (the microphone). Then, if NOTARY_PROFILE is also set, it is submitted to Apple
#                   with notarytool, the ticket is stapled to the bundle, and the zip is made from
#                   the stapled app - that build opens on any Mac with no warning at all.
#   TEAM_ID         the ten-character team id (optional when DEVELOPER_ID names it in brackets).
#   NOTARY_PROFILE  the notarytool keychain profile made once with
#                       xcrun notarytool store-credentials "<profile>" --apple-id <id> --team-id <TEAM_ID>
#                   (an app-specific password from appleid.apple.com). Unset: signed, not notarized,
#                   and NOTES.txt says so.
#
# BUILD-RUN-SHARE.md has the whole story, and what to tell the tester in each case.
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
        -h|--help)   sed -n '2,30p' "$0"; exit 0 ;;
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
ENTITLEMENTS="$(pwd)/scripts/DLIVE.entitlements"

DEVELOPER_ID="${DEVELOPER_ID:-}"
TEAM_ID="${TEAM_ID:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"
MODE=adhoc
if [ -n "$DEVELOPER_ID" ]; then
    MODE=signed
    # Developer ID, hardened runtime, secure timestamp, entitlements: the four things notarization
    # requires. Not --deep: Apple deprecated it for signing, and the bundle has no nested code -
    # if it ever does, sign it inside-out here. --force because the linker already put a signature there.
    codesign --force --sign "$DEVELOPER_ID" --options runtime --timestamp \
             --entitlements "$ENTITLEMENTS" --identifier com.dine.dlive "$APP"
else
    # A complete ad-hoc signature over the whole bundle, replacing the partial one the linker leaves behind.
    codesign --force --deep --sign - --identifier com.dine.dlive "$APP"
fi
codesign --verify --deep --strict --verbose=2 "$APP"

mkdir -p "$OUT_DIR"
STAMP=$(date +%Y%m%d)
ZIP="$OUT_DIR/DLIVE-$VERSION-$STAMP.zip"
rm -f "$ZIP"

# ditto, not zip: an .app is full of symlinks and a plain zip flattens them, which is
# itself a way to arrive as "damaged".
make_zip() { rm -f "$ZIP"; ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"; }
make_zip

if [ "$MODE" = "signed" ] && [ -n "$NOTARY_PROFILE" ]; then
    MODE=notarized
    # Apple scans the zip, not the app; the ticket comes back for the bundle's signature, gets
    # stapled INTO the bundle, and the zip is made again from the stapled app so what is sent
    # opens offline on a Mac that has never heard of it.
    team_args=()
    [ -n "$TEAM_ID" ] && team_args=(--team-id "$TEAM_ID")
    echo "notarizing $ZIP (this waits for Apple, usually a few minutes)..."
    if ! xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" "${team_args[@]}" --wait; then
        echo "notarization failed - the log has the reason:" >&2
        echo "  xcrun notarytool log <submission-id> --keychain-profile \"$NOTARY_PROFILE\"" >&2
        exit 1
    fi
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"
    spctl --assess --type execute --verbose=2 "$APP"
    make_zip
fi

case "$MODE" in
    notarized) cat > "$OUT_DIR/NOTES.txt" <<'NOTES'
DLIVE

1. Unzip, drag DLIVE.app to Applications, open it. It is signed with a Developer ID and
   notarized by Apple, so there is nothing to allow.

2. It will ask for microphone access the first time it opens an audio device.
   That is DLIVE reading the inputs of your interface or console; say yes or it
   has nothing to mix. System Settings > Privacy & Security > Microphone if it
   is ever refused by accident.

Needs macOS 11 or later. Sessions are written to ~/Music/DLIVE/.
No console? File > Import Multitrack Folder... turns a folder of stems into a
session you can mix, tune and export.
NOTES
    ;;
    signed) cat > "$OUT_DIR/NOTES.txt" <<'NOTES'
DLIVE - signed build (not notarized)

1. Unzip, drag DLIVE.app to Applications.
2. The FIRST time only: right-click (or Control-click) the app and choose Open,
   then Open again in the dialog. It is signed with a Developer ID but has not
   been through Apple's notarization, so macOS wants to be told once.

3. It will ask for microphone access the first time it opens an audio device.
   That is DLIVE reading the inputs of your interface or console; say yes or it
   has nothing to mix. System Settings > Privacy & Security > Microphone if it
   is ever refused by accident.

Needs macOS 11 or later. Sessions are written to ~/Music/DLIVE/.
No console? File > Import Multitrack Folder... turns a folder of stems into a
session you can mix, tune and export.
NOTES
    ;;
    *) cat > "$OUT_DIR/NOTES.txt" <<'NOTES'
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
    ;;
esac

echo
echo "  $ZIP"
echo "  $(du -h "$ZIP" | cut -f1)  $ARCHS  version $VERSION  signed: $MODE"
echo "  notes: $OUT_DIR/NOTES.txt"
