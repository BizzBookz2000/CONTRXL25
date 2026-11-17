#!/bin/bash
# ==========================================================================
#  CONTRXL 25 - build the macOS installer
#  Result: installers/CONTRXL25-<version>-Mac.dmg   containing
#            "Install CONTRXL 25.pkg"        - the installer wizard
#            "CONTRXL 25 User's Manual.pdf"
#            "Uninstall CONTRXL 25.command"
#
#  1. Builds the app with build_mac.sh (skipped if dist/ is up to date;
#     "bash make_installer_mac.sh rebuild" forces a fresh build).
#  2. Makes the installer package: Welcome > License > Install > Summary,
#     installs into /Applications (Intel + Apple silicon, macOS 10.13+).
#  3. Puts it in a disk image (.dmg) - the file you give to people.
#
#  Optional - for Macs that are not yours (no "unidentified developer" warning):
#  with an Apple Developer ID ($99/year) set these before running:
#     export DEVELOPER_ID_APP="Developer ID Application: Your Name (TEAMID)"
#     export DEVELOPER_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)"
#     export NOTARY_PROFILE="contrxl"   # made once with: xcrun notarytool store-credentials contrxl
#  Without them the installer still works; see installer/README.md.
# ==========================================================================
set -u
cd "$(dirname "$0")"
HERE="$(pwd)"
fail() { printf "\n  ERROR: %s\n\n" "$*"; exit 1; }

VERSION=$(sed -n 's/^project(CONTRXL25 VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
[ -n "$VERSION" ] || fail "Could not read the version from CMakeLists.txt."
ID="com.illahtainment.contrxl25"
echo "=========================================================="
echo "  CONTRXL 25 $VERSION - macOS installer builder"
echo "=========================================================="

# ---- 1. the app
APP="dist/CONTRXL 25.app"
if [ "${1:-}" = "rebuild" ] || [ ! -d "$APP" ] || [ ! -x "$APP/Contents/MacOS/CONTRXL25_PluginScanner" ]; then
    echo "  Building the app first (build_mac.sh)..."
    CONTRXL_NO_INSTALL=1 bash build_mac.sh || fail "The app build failed - fix that first."
fi
[ -d "$APP" ] || fail "$APP is missing."
BUILT=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist" 2>/dev/null)
[ "$BUILT" = "$VERSION" ] || echo "  NOTE: dist/ holds version $BUILT, CMakeLists.txt says $VERSION - run with 'rebuild' to update it."
command -v pkgbuild >/dev/null && command -v productbuild >/dev/null || fail "pkgbuild / productbuild not found (they come with macOS and the Command Line Tools)."

WORK="build-installer"
rm -rf "$WORK"; mkdir -p "$WORK/root" "$WORK/resources" "$WORK/dmg" installers
ditto "$APP" "$WORK/root/CONTRXL 25.app"
STAGED="$WORK/root/CONTRXL 25.app"
xattr -cr "$STAGED" 2>/dev/null

# ---- 2a. sign the app (Developer ID + hardened runtime, or ad-hoc for this Mac)
if [ -n "${DEVELOPER_ID_APP:-}" ]; then
    echo "  Signing with: $DEVELOPER_ID_APP"
    ENT="installer/mac/entitlements.plist"
    codesign --force --timestamp --options runtime --entitlements "$ENT" --sign "$DEVELOPER_ID_APP" \
        "$STAGED/Contents/MacOS/CONTRXL25_PluginScanner" || fail "Signing the plug-in scanner failed."
    codesign --force --timestamp --options runtime --entitlements "$ENT" --sign "$DEVELOPER_ID_APP" "$STAGED" || fail "Signing the app failed."
    codesign --verify --deep --strict "$STAGED" || fail "The signature does not verify."
else
    echo "  No Developer ID set - ad-hoc signing (fine on your own Macs, see installer/README.md)."
    codesign --force --deep --sign - "$STAGED" >/dev/null 2>&1
fi

# ---- 2b. the package: installs into /Applications, always in place (not "relocated" to another copy)
pkgbuild --analyze --root "$WORK/root" "$WORK/component.plist" >/dev/null || fail "pkgbuild --analyze failed."
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$WORK/component.plist" 2>/dev/null
/usr/libexec/PlistBuddy -c "Set :0:BundleOverwriteAction upgrade" "$WORK/component.plist" 2>/dev/null
pkgbuild --root "$WORK/root" --component-plist "$WORK/component.plist" --install-location /Applications \
         --identifier "$ID" --version "$VERSION" "$WORK/CONTRXL25-app.pkg" >/dev/null || fail "pkgbuild failed."

# installer pages (version filled in), license, background
for f in welcome.html conclusion.html; do
    sed "s/__VERSION__/$VERSION/g" "installer/mac/resources/$f" > "$WORK/resources/$f"
done
cp installer/mac/resources/background*.png "$WORK/resources/"
cp installer/common/EULA.txt "$WORK/resources/License.txt"
sed "s/__VERSION__/$VERSION/g" installer/mac/distribution.xml > "$WORK/distribution.xml"

PKG="$WORK/dmg/Install CONTRXL 25.pkg"
SIGNARGS=()
[ -n "${DEVELOPER_ID_INSTALLER:-}" ] && SIGNARGS=(--sign "$DEVELOPER_ID_INSTALLER" --timestamp)
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
             --version "$VERSION" ${SIGNARGS[@]+"${SIGNARGS[@]}"} "$PKG" || fail "productbuild failed."

# ---- 2c. notarize the installer (only with a Developer ID + notary profile)
if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "${DEVELOPER_ID_INSTALLER:-}" ]; then
    echo "  Notarizing (Apple checks it - usually 2 to 10 minutes)..."
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait || fail "Notarization failed (see above)."
    xcrun stapler staple "$PKG" || fail "Stapling failed."
fi

# ---- 3. the disk image
[ -f "docs/CONTRXL 25 User's Manual.pdf" ] && cp "docs/CONTRXL 25 User's Manual.pdf" "$WORK/dmg/"
cp "installer/mac/Uninstall CONTRXL 25.command" "$WORK/dmg/"; chmod +x "$WORK/dmg/Uninstall CONTRXL 25.command"
cp installer/common/EULA.txt "$WORK/dmg/License.txt"
DMG="installers/CONTRXL25-$VERSION-Mac.dmg"
rm -f "$DMG"
hdiutil create -volname "CONTRXL 25 $VERSION" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null || fail "Making the disk image failed."
[ -n "${DEVELOPER_ID_APP:-}" ] && codesign --sign "$DEVELOPER_ID_APP" --timestamp "$DMG" >/dev/null 2>&1
if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "${DEVELOPER_ID_APP:-}" ]; then
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait >/dev/null && xcrun stapler staple "$DMG" >/dev/null
fi

echo
echo "  =========================================================="
echo "   Done!  $HERE/$DMG"
echo "   Open it and double-click \"Install CONTRXL 25.pkg\"."
echo "  =========================================================="
open -R "$DMG" 2>/dev/null
exit 0
