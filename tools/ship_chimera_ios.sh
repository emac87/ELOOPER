#!/bin/bash
# Build Chimera's iPad/iPhone app (AUv3 inside) on the Mac and send it to TestFlight.
# Run from a clone of this repo (~/Documents/ELooper); the "Ship Chimera to TestFlight" launcher pulls first.
# Logs: tools/generate-ios.log (CMake) and build-ios/tools/testflight.log (archive + upload)
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
cd "$(dirname "$0")/.." || exit 1
WBROOT="$HOME/Documents/Claude Workbench"; WB="$WBROOT/scripts/testflight"; source "$WB/asc.env"
OUT="build-ios"; mkdir -p tools "$OUT/tools"; GEN="tools/generate-ios.log"; : > "$GEN"; : > "$OUT/tools/testflight.log"
command -v cmake >/dev/null || brew install cmake >> "$GEN" 2>&1
JUCE_ARG=""; [ -f "$WBROOT/JUCE/CMakeLists.txt" ] && JUCE_ARG="-DELOOPER_JUCE_DIR=$WBROOT/JUCE"
BUILD=$(date +%Y%m%d%H%M)
echo "Generating the iOS Xcode project for Chimera (build $BUILD)..." | tee -a "$GEN"
cmake -S . -B "$OUT" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 $JUCE_ARG \
  -DCHIMERA_TEAM_ID="$TEAM_ID" -DCHIMERA_BUILD="$BUILD" \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$TEAM_ID" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic \
  -DCMAKE_XCODE_GENERATE_SCHEME=ON >> "$GEN" 2>&1
if [ $? -ne 0 ]; then echo "GENERATE FAILED — see $GEN" | tee -a "$OUT/tools/testflight.log"; exit 1; fi
echo "GENERATED" >> "$GEN"
SHIP_BUNDLE="com.evanmcintosh.chimera" bash "$WB/ship_testflight.sh" "$PWD/$OUT" "Chimera_Standalone"
