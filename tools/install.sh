#!/bin/bash
# ELooper installer / updater for macOS.
#   curl -fsSL https://raw.githubusercontent.com/emac87/ELOOPER/elooper-initial/tools/install.sh | bash
# Downloads the newest successful build, installs it to /Applications, adds it to the Dock
# and opens it. Files downloaded with curl are not quarantined, so Gatekeeper won't block it.
set -euo pipefail

REPO="emac87/ELOOPER"
BRANCH="elooper-initial"
URL="https://nightly.link/${REPO}/workflows/build/${BRANCH}/ELooper-macos.zip"
DEST="/Applications/ELooper.app"

[ "$(uname)" = "Darwin" ] || { echo "This installer is for macOS."; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "→ Downloading the newest ELooper…"
curl -fL --progress-bar -o "$TMP/download.zip" "$URL"

echo "→ Unpacking…"
/usr/bin/ditto -x -k "$TMP/download.zip" "$TMP/outer"
INNER="$(find "$TMP/outer" -name 'ELooper-macos.zip' -maxdepth 2 | head -1)"
if [ -n "$INNER" ]; then
    /usr/bin/ditto -x -k "$INNER" "$TMP/app"
else
    mv "$TMP/outer" "$TMP/app"
fi
APP="$(find "$TMP/app" -name 'ELooper.app' -maxdepth 2 -type d | head -1)"
[ -n "$APP" ] && [ -x "$APP/Contents/MacOS/ELooper" ] || { echo "Download didn't contain ELooper.app"; exit 1; }

echo "→ Installing to /Applications…"
osascript -e 'tell application "ELooper" to quit' >/dev/null 2>&1 || true
sleep 1
if [ -w /Applications ]; then SUDO=""; else SUDO="sudo"; echo "  (asking for your password to write to /Applications)"; fi
$SUDO rm -rf "$DEST"
$SUDO /usr/bin/ditto "$APP" "$DEST"
$SUDO xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

if ! defaults read com.apple.dock persistent-apps 2>/dev/null | grep -q "ELooper.app"; then
    echo "→ Adding ELooper to your Dock…"
    defaults write com.apple.dock persistent-apps -array-add \
      "<dict><key>tile-data</key><dict><key>file-data</key><dict><key>_CFURLString</key><string>file://${DEST}/</string><key>_CFURLStringType</key><integer>15</integer></dict></dict></dict>"
    killall Dock || true
fi

echo "✓ ELooper is installed. It updates itself from now on — just click it in the Dock."
open "$DEST"
