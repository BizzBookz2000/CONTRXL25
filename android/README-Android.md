# CONTRXL 25 for Android

`dist/CONTRXL25-web-v5.apk` is the CONTRXL 25 web app (`../web/CONTRXL25.html`) as an Android app. It runs fully
offline, needs Android 7.0 or newer, and works best on a tablet or a phone held sideways (the app opens in landscape).

## Install it on a phone or tablet

1. Copy `CONTRXL25-web-v5.apk` to the device (USB cable, Google Drive, email to yourself...).
2. Tap the file. Android asks to allow installing apps from that source (Files, Drive, Chrome...): tap **Settings**,
   turn on **Allow from this source**, go back and tap **Install**.
3. If Google Play Protect says it doesn't recognise the app, tap **More details > Install anyway**. It warns about
   any app that isn't from the Play Store.
4. Open **CONTRXL 25** from the app drawer.

To update, install a newer .apk the same way. Your songs, library and settings stay, as long as the new .apk is signed
with the same key (see *Signing key* below).

## What works on Android

| Feature | On Android |
|---|---|
| Pads, keys, sequencer, song chain, effects, chopping | Same as the web version; touch the pads and keys |
| **Sample** (microphone / audio interface) | Asks for microphone permission the first time. USB audio interfaces work as the input on most devices |
| **Resample** | Yes |
| Load samples, + Samples, Load song, library import | Opens Android's file picker (phone storage, Downloads, Google Drive...) |
| Save .wav, Save song, exports | Saved to **Download/CONTRXL 25** on the device, with a message saying where |
| MIDI keyboards | **USB** keyboards (USB-C or an OTG adapter) appear as soon as they're plugged in, including the Akai MPK mini. Bluetooth MIDI keyboards work once Android has connected them (for example through the maker's app) |
| Sample library | Stored inside the app; backup / restore through the library's More menu |
| VST3 / Audio Unit plug-ins | No: Android has no desktop plug-ins (the desktop app hosts them) |

Tips: pinch to zoom on a small phone. The screen stays on while the app is open. Use wired headphones or the speaker:
Bluetooth headphones add a delay you can hear when playing.

## Building the .apk yourself (Ubuntu / Debian)

```
sudo apt install android-sdk-platform-23 aapt dalvik-exchange apksigner zipalign default-jdk
bash build_apk.sh                         # uses ../web/CONTRXL25.html
bash build_apk.sh path/to/newer.html      # package a newer web version
```

Change `android:versionCode` (a whole number, higher each release) and `android:versionName` in
`AndroidManifest.xml` before building an update.

## Signing key

`keystore/contrxl25-release.jks` + `keystore/PASSWORD.txt` sign the app. **Keep a backup of this folder.** Android only
installs an update over the old app when both are signed with the same key. If the key is lost, the app has to be
uninstalled (which deletes its library and settings) before a version signed with a new key can be installed.

## How it is built

- `src/.../MainActivity.java`: full-screen WebView. Serves `assets/www/` from `https://appassets.androidplatform.net/`
  (a secure origin, so the microphone and storage work). Handles the microphone permission, the file picker and saving
  to Downloads. Pinch zoom, keep-screen-on, immersive full screen, and asks before leaving with Back.
- `src/.../MidiBridge.java`: opens every MIDI source through `android.media.midi` and passes whole MIDI messages to the page.
- `www-shim.js` (becomes `assets/www/android-shim.js`, loaded first): gives the page Web MIDI (`navigator.requestMIDIAccess`)
  on top of MidiBridge, and turns the app's download links into saves to Downloads.

## Native version (the JUCE desktop engine)

The desktop app (C++ / JUCE) can also be built for Android with the Android NDK in Android Studio. That would bring
the desktop engine and its features, but not VST3 / AU plug-ins, which don't exist on Android.
