#!/bin/bash
# ==========================================================================
#  CONTRXL 25 - one-step macOS build (Intel or Apple silicon)
#  Needs: Apple's Command Line Tools (or Xcode). Everything else is fetched:
#    - CMake (if you don't have it) -> ./tools/CMake.app
#    - JUCE 8.0.12                  -> ./JUCE
#  The first run takes 5-15 minutes. Result: dist/CONTRXL 25.app
#  Run it from Terminal:   bash build_mac.sh
#  or double-click "Build CONTRXL 25.command" in Finder.
# ==========================================================================
set -u
cd "$(dirname "$0")"
HERE="$(pwd)"
JUCEVER="8.0.12"
CMAKEVER="3.31.12"

say()  { printf "\n  %s\n" "$*"; }
fail() { printf "\n  ERROR: %s\n\n" "$*"; exit 1; }

echo "=========================================================="
echo "  CONTRXL 25 - macOS build   ($(uname -m), macOS $(sw_vers -productVersion 2>/dev/null))"
echo "=========================================================="

case "$HERE" in
    /Volumes/*)
        echo
        echo "  NOTE: this folder is on an external / USB drive ($HERE)."
        echo "  Thumb drives formatted for Windows (exFAT / FAT32) can break the app and are slow."
        echo "  If the build fails or the app won't open, copy the CONTRXL25 folder to your Mac first:"
        echo "      cp -R \"$HERE\" ~/Developer/   then run build_mac.sh from there."
        echo ;;
esac

# ---- 1. compiler (Command Line Tools or Xcode)
if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang++ >/dev/null 2>&1; then
    say "Apple's Command Line Tools are not installed. A window will ask you to install them."
    say "Click Install, wait until it finishes (5-15 min), then run this script again."
    xcode-select --install 2>/dev/null
    exit 1
fi
echo "  Compiler: $(xcrun clang++ --version | head -1)"

# ---- 2. CMake 3.22 or newer
CMAKE=""
for c in "$(command -v cmake 2>/dev/null)" /Applications/CMake.app/Contents/bin/cmake \
         /opt/homebrew/bin/cmake /usr/local/bin/cmake "$HERE/tools/CMake.app/Contents/bin/cmake"; do
    if [ -n "$c" ] && [ -x "$c" ]; then
        v=$("$c" --version | head -1 | awk '{print $3}')
        major=${v%%.*}; rest=${v#*.}; minor=${rest%%.*}
        if [ "$major" -gt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -ge 22 ]; }; then CMAKE="$c"; break; fi
    fi
done
if [ -z "$CMAKE" ]; then
    say "CMake not found - downloading CMake $CMAKEVER into ./tools (one time, about 50 MB)..."
    mkdir -p tools
    url="https://github.com/Kitware/CMake/releases/download/v$CMAKEVER/cmake-$CMAKEVER-macos-universal.tar.gz"
    curl -L --fail --retry 3 -o tools/cmake.tgz "$url" || fail "Could not download CMake. Install it from https://cmake.org/download/ (the macOS .dmg) and run this script again."
    tar -xzf tools/cmake.tgz -C tools || fail "Could not unpack CMake."
    rm -f tools/cmake.tgz
    rm -rf tools/CMake.app
    mv tools/cmake-$CMAKEVER-macos-universal/CMake.app tools/ && rm -rf tools/cmake-$CMAKEVER-macos-universal
    xattr -dr com.apple.quarantine tools/CMake.app 2>/dev/null
    CMAKE="$HERE/tools/CMake.app/Contents/bin/cmake"
    [ -x "$CMAKE" ] || fail "CMake download looks incomplete. Delete the tools folder and try again."
fi
echo "  CMake:    $("$CMAKE" --version | head -1)  ($CMAKE)"

# ---- 3. JUCE
JUCEPATH="$HERE/JUCE"
if [ ! -f "$JUCEPATH/CMakeLists.txt" ]; then
    for d in "$HERE/JUCE/JUCE-$JUCEVER" "$HERE/JUCE-$JUCEVER" "$HERE/../JUCE" "$HOME/JUCE"; do
        if [ -f "$d/CMakeLists.txt" ]; then JUCEPATH="$(cd "$d" && pwd)"; break; fi
    done
fi
if [ ! -f "$JUCEPATH/CMakeLists.txt" ]; then
    say "Downloading JUCE $JUCEVER (about 70 MB, one time only)..."
    curl -L --fail --retry 5 --retry-delay 3 -C - -o juce_download.zip \
        "https://github.com/juce-framework/JUCE/archive/refs/tags/$JUCEVER.zip" \
        || fail "Could not download JUCE. Get 'Source code (zip)' from https://github.com/juce-framework/JUCE/releases/tag/$JUCEVER, unzip it, rename the folder to JUCE and put it next to this script."
    rm -rf "JUCE-$JUCEVER"
    unzip -q juce_download.zip || fail "Could not unpack JUCE."
    rm -f juce_download.zip
    [ -e JUCE ] && mv JUCE "JUCE_old_$RANDOM"
    mv "JUCE-$JUCEVER" JUCE
    JUCEPATH="$HERE/JUCE"
fi
echo "  JUCE:     $JUCEPATH"

# ---- 4. configure + build
GEN="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GEN="Ninja"
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
"$CMAKE" -S . -B build-mac -G "$GEN" -DCMAKE_BUILD_TYPE=Release "-DJUCE_DIR=$JUCEPATH" || fail "CMake configure failed (see above). Delete the build-mac folder and try again."
"$CMAKE" --build build-mac --config Release --parallel "$JOBS" || fail "The build failed (see the first 'error:' above)."

# ---- 5. copy the app to dist/, sign it for this Mac, open the folder
APP="build-mac/CONTRXL25_artefacts/Release/CONTRXL 25.app"
[ -d "$APP" ] || fail "Build finished but $APP is missing."
mkdir -p dist
rm -rf "dist/CONTRXL 25.app"
ditto "$APP" "dist/CONTRXL 25.app"
codesign --force --deep --sign - "dist/CONTRXL 25.app" >/dev/null 2>&1 || echo "  (ad-hoc signing skipped)"
xattr -dr com.apple.quarantine "dist/CONTRXL 25.app" 2>/dev/null

# make_installer_mac.sh sets CONTRXL_NO_INSTALL=1: build into dist/ only (the installer installs it)
if [ "${CONTRXL_NO_INSTALL:-}" = "1" ]; then echo "  Built: $HERE/dist/CONTRXL 25.app"; exit 0; fi

# ---- 6. install into /Applications (so it shows in Launchpad / Spotlight)
INSTALLED=""
if ditto "dist/CONTRXL 25.app" "/Applications/CONTRXL 25.app.new" 2>/dev/null; then
    rm -rf "/Applications/CONTRXL 25.app"
    mv "/Applications/CONTRXL 25.app.new" "/Applications/CONTRXL 25.app"
    codesign --force --deep --sign - "/Applications/CONTRXL 25.app" >/dev/null 2>&1
    xattr -dr com.apple.quarantine "/Applications/CONTRXL 25.app" 2>/dev/null
    INSTALLED="yes"
fi

echo
echo "  =========================================================="
if [ -n "$INSTALLED" ]; then
echo "   Done!  CONTRXL 25 is installed in Applications"
echo "   (a copy is also in $HERE/dist)"
else
echo "   Done!  $HERE/dist/CONTRXL 25.app"
echo "   (could not copy it to Applications - drag it there yourself)"
fi
echo "  =========================================================="
if [ -n "$INSTALLED" ]; then open -R "/Applications/CONTRXL 25.app" 2>/dev/null; else open dist 2>/dev/null; fi
exit 0
