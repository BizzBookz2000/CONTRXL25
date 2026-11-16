# CONTRXL 25 for iPad

The same app as the Windows version (same source code), built for iPadOS with Xcode.

## What you need

* A Mac with **Xcode 15 or newer** (free from the Mac App Store).
* An iPad with **iPadOS 15 or newer**, and its USB cable.
* An Apple ID. A free one works for putting the app on your own iPad (it has to be
  re-installed from Xcode every 7 days); a paid Apple Developer account ($99/year) removes that limit
  and lets you use TestFlight / the App Store.

## Open and run it

1. Unzip `CONTRXL25_iPad_Xcode.zip` on the Mac. Keep the folder layout as it is: the Xcode project
   finds JUCE in the `JUCE` folder next to it.
2. Double-click **`Builds/iOS/CONTRXL25.xcodeproj`**. Xcode opens.
3. In Xcode's left column click **CONTRXL25** (the blue icon at the top), then the target
   **CONTRXL25 - App**, then the **Signing & Capabilities** tab:
   * **Team**: pick your Apple ID (Xcode > Settings > Accounts adds it if it isn't listed).
   * If Xcode says the bundle identifier is taken, change `com.illahtainment.contrxl25` to something
     of your own, e.g. `com.yourname.contrxl25`.
4. Plug in the iPad, unlock it, tap **Trust**. Pick it at the top of the Xcode window (next to the
   play button). On iPadOS 16+, also turn on **Settings > Privacy & Security > Developer Mode** on
   the iPad when asked (the iPad restarts).
5. Press **Run** (the play button, or Cmd+R). The first build takes a few minutes.
6. With a free Apple ID the first launch says "Untrusted Developer": on the iPad open
   **Settings > General > VPN & Device Management**, tap your Apple ID, **Trust**. Start the app again.

Tip: for the fastest app, choose **Product > Scheme > Edit Scheme > Run > Build Configuration: Release**.

## What is different on the iPad

| | Windows | iPad |
|---|---|---|
| Plug-ins | VST3 | **AUv3** (install them from the App Store, open each once, then Mixer > Plug-ins... > Scan) |
| Right-click menus | right mouse button | **hold your finger** on the pad / slot / button for half a second |
| Library folder | any folder | **Files app > On My iPad > CONTRXL 25 > Library** - copy samples in there, then press Refresh |
| Saving songs | Save dialog | saved straight into **Files > On My iPad > CONTRXL 25 > Songs** |
| Exports (WAV, stems, chops, MIDI) | Save dialog | **Files > On My iPad > CONTRXL 25 > Exports** |
| Loading audio / songs | Open dialog | the iPad's file picker (iCloud Drive, On My iPad, USB drives...) |
| MIDI keyboards | USB | USB (via a USB-C / Lightning adapter) or **Bluetooth**: MIDI... > **Bluetooth MIDI...** pairs it |
| Audio interface | ASIO / WASAPI | any class-compliant USB interface, or the built-in speaker / mic |
| Closing | quit the app | the app saves the library and settings when it goes to the background; an unsaved song is kept as **Songs > Autosave - (name).zip** |

The Akai MPK mini IV / MPK mini 3 profiles, MIDI Learn, custom keyboard sizes, the sequencer, chops,
mixer and effects all work the same. A computer keyboard (Magic Keyboard / Bluetooth keyboard)
plays the keys like on Windows.

## Making changes

The C++ code in `Source/` is shared with the Windows build. To add or remove source files, open
`CONTRXL25_iPad.jucer` with JUCE's **Projucer** (in the JUCE download) and press *Save*; it rewrites
`Builds/iOS/CONTRXL25.xcodeproj`. For edits to existing files you only need Xcode.

## If it doesn't build

* **"Signing for CONTRXL25 requires a development team"** - step 3 above.
* **"Failed to register bundle identifier"** - change the bundle identifier (step 3).
* **"iOS 15.0 is not installed" / no iPad in the list** - Xcode > Settings > Platforms: download iOS.
* **"'juce_core/juce_core.h' file not found"** - the `JUCE` folder must sit next to `Builds` (step 1).
