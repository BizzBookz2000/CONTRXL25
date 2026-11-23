#!/bin/bash
# ==========================================================================
#  CONTRXL 25 for Android - build the .apk
#  Needs (Ubuntu / Debian):  sudo apt install android-sdk-platform-23 aapt dalvik-exchange apksigner zipalign default-jdk
#  Usage:  bash build_apk.sh [path/to/CONTRXL web app .html]     (default: ../web/CONTRXL25.html)
#  Result: dist/CONTRXL25-<version>.apk   (install it on a phone or tablet: see README-Android.md)
# ==========================================================================
set -euo pipefail
cd "$(dirname "$0")"
HTML="${1:-../web/CONTRXL25.html}"
SDK=/usr/lib/android-sdk
JAR=$(ls -d $SDK/platforms/android-*/android.jar | sort -V | tail -1)
BT=$(ls -d $SDK/build-tools/*/ | sort -V | tail -1)
DX=$(command -v dalvik-exchange || echo "$BT/dx")
VERSION=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' AndroidManifest.xml | tr ' ' '-')
[ -f "$JAR" ] || { echo "android.jar not found - sudo apt install android-sdk-platform-23"; exit 1; }
[ -f "$HTML" ] || { echo "web app not found: $HTML"; exit 1; }

echo "== CONTRXL 25 for Android ($VERSION) =="
rm -rf build && mkdir -p build/gen build/classes build/apk assets/www dist

# 1. the web app + the Android shim (loaded first)
python3 - "$HTML" <<'PY'
import sys, re
src = open(sys.argv[1], encoding='utf-8').read()
tag = '<script src="android-shim.js"></script>'
src = src.replace('<head>', '<head>' + tag, 1) if '<head>' in src else tag + src
open('assets/www/index.html', 'w', encoding='utf-8').write(src)
PY
cp www-shim.js assets/www/android-shim.js

# 2. resources -> R.java
aapt package -f -m -M AndroidManifest.xml -S res -I "$JAR" -J build/gen

# 3. Java -> classes -> classes.dex
javac -nowarn -Xlint:-options -source 8 -target 8 -encoding UTF-8 -bootclasspath "$JAR" -classpath "$JAR" \
      -d build/classes $(find src build/gen -name '*.java')
"$DX" --dex --min-sdk-version=24 --output=build/apk/classes.dex build/classes

# 4. package (web assets stored uncompressed would be faster to read, but compressed keeps the .apk small)
aapt package -f -M AndroidManifest.xml -S res -A assets -I "$JAR" -F build/unsigned.apk
( cd build/apk && aapt add ../unsigned.apk classes.dex >/dev/null )
zipalign -f -p 4 build/unsigned.apk build/aligned.apk

# 5. sign. Keep keystore/ safe: updates must be signed with the same key or Android refuses to install them over the old version.
mkdir -p keystore
KS=keystore/contrxl25-release.jks
if [ ! -f "$KS" ]; then
  echo "  Making a new signing key: $KS (password in keystore/PASSWORD.txt)"
  PW=$(head -c 18 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 20)
  echo "$PW" > keystore/PASSWORD.txt
  keytool -genkeypair -keystore "$KS" -storepass "$PW" -keypass "$PW" -alias contrxl25 -keyalg RSA -keysize 3072 \
          -validity 10000 -dname "CN=Illahtainment, O=Illahtainment, C=US" >/dev/null 2>&1
fi
PW=$(cat keystore/PASSWORD.txt)
OUT="dist/CONTRXL25-$VERSION.apk"
apksigner sign --ks "$KS" --ks-pass "pass:$PW" --ks-key-alias contrxl25 --min-sdk-version 24 --out "$OUT" build/aligned.apk
apksigner verify "$OUT"
echo "  Done: $OUT ($(du -h "$OUT" | cut -f1))"
