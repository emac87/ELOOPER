#!/usr/bin/env bash
# Build ELooper on macOS and run the tests.  Usage: tools/build-mac.sh [--open]
# Needs: Xcode Command Line Tools (xcode-select --install) and CMake >= 3.22 (brew install cmake ninja).
set -euo pipefail
cd "$(dirname "$0")/.."
command -v cmake >/dev/null || { echo "CMake not found: brew install cmake ninja"; exit 1; }
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN=Ninja
# Native architecture only for a faster local build (CI/release can build universal).
cmake -S . -B build-mac -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
cmake --build build-mac --config Release -j "$(sysctl -n hw.ncpu)"
ctest --test-dir build-mac --output-on-failure
APP=build-mac/ELooper_artefacts/Release/ELooper.app
codesign --force --deep --sign - "$APP"   # ad-hoc signature (no developer account needed)
echo "Built: $APP"
[ "${1:-}" = "--open" ] && open "$APP"
