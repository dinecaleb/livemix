#!/usr/bin/env bash
# Vendors JUCE into external/JUCE (shallow clone, pinned) and configures the build.
set -euo pipefail
cd "$(dirname "$0")/.."
JUCE_TAG="8.0.8"
if [ ! -d external/JUCE ]; then
  git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git external/JUCE
fi
export PATH="$HOME/.local/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
echo "Configured. Build with: scripts/build.sh"
