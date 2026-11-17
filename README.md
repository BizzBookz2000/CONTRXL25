# CONTRXL 25 — desktop edition (JUCE / C++)

A native Windows and macOS (and Linux) rebuild of the CONTRXL 25 web instrument: 64 pads in 8 banks,
a 25/32-key keyboard, chops, a 16-pattern sequencer with song chain, per-pad effects, retro sampler
modes, stem separation, time-stretch, sampling/resampling and DAW-aligned exports.

Song files are **the same `.zip` format as the web version**, so you can save a song in the browser
and open it here, and the other way round.

---

## 1. Build it on your PC (Windows 10/11)

### What to install (one time, all free)

1. **Visual Studio Community** (2026, 2022 or 2019) — https://visualstudio.microsoft.com/vs/community/
   In the installer, tick the workload **"Desktop development with C++"**.
   Make sure **"C++ CMake tools for Windows"** is ticked on the right (it is by default).
2. That's it. `build_windows.bat` downloads JUCE (the audio framework) into a `JUCE` folder the first time.
   You need an internet connection for that first build only.

### Option A — one click

1. Unzip this folder somewhere simple, for example `C:\dev\CONTRXL25`
   (avoid very long paths and OneDrive-synced folders).
2. Double-click **`build_windows.bat`**.
3. Wait. The first build takes about 3–8 minutes (JUCE is downloaded and compiled once).
4. A folder opens with **`dist\CONTRXL 25.exe`**. Double-click it to play.
   The `.exe` is self-contained — you can copy it anywhere or to another PC.

### Option B — inside Visual Studio (to edit and debug the code)

1. Start Visual Studio → **Open a local folder** → pick the `CONTRXL25` folder.
2. Visual Studio reads `CMakeLists.txt` and configures by itself (watch the Output window;
   the first time it downloads JUCE).
3. In the toolbar, set the configuration to **x64-Release** (or x64-Debug for debugging) and
   the startup item to **CONTRXL 25.exe**.
4. Press **F5** (run with debugger) or **Ctrl+F5** (run without).

### Option C — command line

Open the **"x64 Native Tools Command Prompt"** for your Visual Studio (Start menu) in the folder and run:

```bat
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja --parallel
```

The program is at `build-ninja\CONTRXL25_artefacts\Release\CONTRXL 25.exe`.
(Ninja works with every Visual Studio version, so you never need to name a "Visual Studio 17 2022"-style generator.)

### Using a JUCE copy you downloaded yourself (offline builds)

Put JUCE in a folder called `JUCE` next to `CMakeLists.txt`
(so that `CONTRXL25\JUCE\CMakeLists.txt` exists), or pass `-DJUCE_DIR=C:\path\to\JUCE`.
To try a newer JUCE release: `-DCONTRXL_JUCE_VERSION=8.0.x`.

### If something goes wrong

| Message | Fix |
|---|---|
| `Visual Studio was not found` / `No CMAKE_CXX_COMPILER could be found` | Install the **Desktop development with C++** workload in Visual Studio Installer. |
| `CMake was not found` | In Visual Studio Installer → Modify → Individual components → **C++ CMake tools for Windows**. |
| `Build step for juce failed` (from FetchContent) | Old versions of the script let CMake download JUCE. Use the current `build_windows.bat`, delete the `build-ninja` folder, and run it again. |
| Download of JUCE fails | Check the internet connection / firewall, or download `JUCE-8.0.12.zip` from github.com/juce-framework/JUCE/releases, unzip it, rename the folder to `JUCE` and put it next to `CMakeLists.txt`. |
| `The build tools for Visual Studio 2022 (v143) cannot be found` or `could not find any instance of Visual Studio` | A Visual Studio generator for a different year was used. Use the current `build_windows.bat` (it builds with Ninja + your installed compiler) and delete any old `build` folder. |
| `ninja: error: manifest 'build.ninja' still dirty after 100 tries` | Some files are dated in the future (zip from another time zone, or a wrong PC clock). The current `build_windows.bat` fixes the dates itself; otherwise wait a few hours or check the PC clock, then delete `build-ninja` and run again. |
| `tar: ... Can't restore time` while unpacking JUCE | Harmless - the script checks JUCE is complete and carries on. |
| Path too long errors | Move the folder to something short like `C:\dev\CONTRXL25`. |
| Build worked but no sound | Click **Audio / MIDI** in the app and pick your output device. "Windows Audio (Exclusive Mode)" gives the lowest latency. |

## 1b. Build it on your Mac (Intel or Apple silicon, macOS 10.15.4 or newer)

### What to install (one time, all free)

1. **Apple's Command Line Tools** — open Terminal and run `xcode-select --install`, click **Install**.
   (Full Xcode from the App Store also works and adds a debugger.)
2. That's it. `build_mac.sh` downloads CMake (if you don't have it) and JUCE the first time.

### Option A — one step

1. Unzip this folder somewhere simple, for example `~/Developer/CONTRXL25`.
2. Double-click **`Build CONTRXL 25.command`** (first time: right-click it → **Open** → **Open**),
   or in Terminal: `cd ~/Developer/CONTRXL25 && bash build_mac.sh`
3. Wait. The first build takes about 5–15 minutes on an Intel MacBook.
4. The app is installed in **Applications** (a copy stays in `dist/`). Open it from Launchpad or Applications.
   Build from the Mac's own disk (e.g. `~/Developer/CONTRXL25`), not from a USB stick.

### Option B — inside Xcode (to edit and debug the code)

```sh
cmake -S . -B build-xcode -G Xcode -DJUCE_DIR="$PWD/JUCE"
open build-xcode/CONTRXL25.xcodeproj
```
Pick the **CONTRXL25** scheme and **My Mac**, then press **Cmd+R**.

### Option C — command line

```sh
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"
open "build-mac/CONTRXL25_artefacts/Release/CONTRXL 25.app"
```
Universal app (Intel + Apple silicon): add `-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"`.

### Mac notes

* Shortcuts use **Cmd** where Windows uses Ctrl (Cmd+Z, Cmd+S, Cmd+O...). Drag-to-copy uses **Option**.
* **Sample** asks for microphone access the first time (System Settings → Privacy & Security → Microphone).
* Plug-ins: **Audio Units** and **VST3** (`/Library/Audio/Plug-Ins/...` and `~/Library/Audio/Plug-Ins/...`).
* Library folder: `~/Library/Application Support/CONTRXL25/Library` (or any folder you choose).
* Song files are the same on Windows, Mac and the web version. A song that uses a plug-in finds it again on the
  other computer if the same plug-in (same maker / version) is installed there.
* The app is signed only for your own Mac. To give it to someone else, they right-click → Open the first time
  (or you sign and notarize it with an Apple Developer ID).

| Message | Fix |
|---|---|
| `xcrun: error: invalid active developer path` | Run `xcode-select --install` and try again. |
| "Build CONTRXL 25.command" can't be opened | Right-click it → Open, or run `bash build_mac.sh` in Terminal. |
| `Permission denied` | Run it as `bash build_mac.sh` (zip files sometimes lose the "executable" flag). |
| CMake download fails | Install CMake from https://cmake.org/download/ (the macOS `.dmg`), then run the script again. |
| No sound | **Audio / MIDI** in the app → choose the output (e.g. MacBook Pro Speakers or your interface). |
| Sample records silence | Allow CONTRXL 25 under System Settings → Privacy & Security → Microphone. |
| An Audio Unit is missing | Mixer → **Plug-ins...** → **Scan all...** → *Scan for AudioUnit plug-ins*. |
| A plug-in scan fails / keeps failing | Mixer → **Plug-ins...** → **Choose & scan** → tick only the plug-ins you want → **Scan ticked**. Failed ones are skipped; tick them + **Scan ticked** to retry (choose a longer time-out for slow, licence-checking plug-ins). |

---

## 1c. Make a setup program (installer) to give to others

`make_installer_windows.bat` (on a PC) builds `installers\CONTRXL25-<version>-Windows-Setup.exe`: a standard setup
wizard with license agreement, install folder, Start Menu and desktop shortcuts, and an uninstaller in Settings > Apps.
`Make Mac Installer.command` (on a Mac) builds `installers/CONTRXL25-<version>-Mac.dmg` with an installer package
(Introduction > License > Install), the User's Manual and an uninstaller. Details, code signing and the
"unknown publisher" warnings: `installer/README.md`.

## 2. First run

* **Size** (top bar): *Fit* scales the app to your screen; pick a percentage to make it bigger or smaller.
  If the window is smaller than the layout, scroll with the mouse wheel or the scroll bars.
* **Audio / MIDI** (top bar): pick the output, the input (for Sample) and tick your MIDI keyboard.
  New MIDI keyboards are switched on automatically when you plug them in.
  Choose a buffer of 128–256 samples for snappy pads.
* Drag audio files (WAV, AIFF, FLAC, OGG, MP3; M4A/WMA via Windows) onto a pad or the sample window,
  or press **Load**. Several files fill the following pads.
* Open a song saved from the web version with **Song & export → Load song…**, `Ctrl+O`,
  or by dropping the `.zip` on `CONTRXL 25.exe`.

### Keys

| Key | Action |
|---|---|
| `Z`–`M`, `Q`–`I` rows (+ `9 O 0 P [ = ]` on 32 keys) | play the keyboard (QWERTY button on) |
| `Space` | play / stop |
| `←` `→` | octave down / up |
| `[` `]` | previous / next bank (25-key mode) |
| `,` `.` | previous / next chop region |
| `Shift`+`1`…`8` | hit pads 1–8 of the bank |
| `Ctrl+Z`, `Ctrl+Shift+Z` / `Ctrl+Y` (Mac: `Cmd+Z`, `Cmd+Shift+Z`) | undo / redo |
| `Ctrl+C`, `Ctrl+V` (Mac: `Cmd`) | copy / paste pad (or selected steps with the Select tool) |
| `Ctrl+S`, `Ctrl+O` (Mac: `Cmd`) | save / load song |
| `Delete` | clear selected steps |
| `Esc` | close the chop window / cancel "pick a pad / key" / leave MIDI Learn |

Mouse: double-click the sample window to load a sample; **Chop window** pops the sample out for chopping
(Esc closes it); the sequencer and keyboard have **Pop out / Dock** buttons; drag a pad onto another to move it (hold `Ctrl` to copy — `Option` on a Mac); right-click a pad for its menu;
double-click an empty pad to load (Shift+double-click any pad); drag a chop region from the sample window onto a pad to copy it there;
with the sequencer's Select tool, drag selected steps to move them (hold `Ctrl` to copy — `Option` on a Mac); mouse wheel on the waveform zooms (Shift+wheel scrolls);
pitch knobs: drag (Shift = fine), wheel, double-click = 0.

### MIDI and MIDI Learn

Out of the box: notes play the keys (octave follows your keyboard's own octave buttons), CC14–21 / CC70–77
turn the 8 pitch knobs, CC1 = mod wheel, pitch bend = ±2 semitones, transport CCs
94/115 play, 95/116 rec, 93/117 stop, 86/113 loop, 87/112 metronome, 88/114 tap tempo,
plus MIDI Start/Stop/Continue and MMC.

Every controller is different, so **link your own controls with MIDI Learn**:

1. Click **MIDI Learn** in the top bar (it turns green and every control gets an outline).
2. Click the control on screen — a pad, a key, a pitch knob, a bank, PLAY / REC / STOP or any other button, or a slider.
3. Press the button / pad / key or move the knob / fader on your controller. The control gets a green outline and its note or CC.
4. Repeat for as many controls as you like, then click **MIDI Learn** again (or press `Esc`).

Notes and CCs can both be learned; knobs and sliders need a CC. Pads follow the bank you are on. A learned note or CC
replaces its built-in meaning. Links are saved automatically.

**MIDI...** (top bar) opens the MIDI window: the last MIDI message and which device sent it (handy to see what your
transport buttons send), the list of links (Remove / Clear all), *Notes & pitch bend from* (play notes from one keyboard
only, while pads / faders on other devices still work), *Set low key*, *Follow keyboard octave buttons*,
*Built-in transport CCs* (switch off if they clash with your controller) and the audio / MIDI device settings.

If the transport still does nothing: open **MIDI...**, press the button on your controller and look at *Last MIDI*.
Nothing there = that MIDI input is switched off, or the button sends on another port: Komplete Kontrol A/M keyboards
(A25, M32...) have a second port named "... DAW" for their transport — switch it on in the MIDI window's input list,
then learn the buttons. "MMC" or "Start" messages work automatically; anything else can be learned. One control can be
learned from several keyboards (e.g. PLAY on both the A25 and the M32).

### Your own keyboard (25 to 88 keys)

Pick **Your own keyboard...** in the keyboard box and type how many keys it has. The on-screen keys, MIDI, chop mode
and the **chop to** buttons follow (a 49-key keyboard gets a **49** button: 49 equal chops, one per key). If the lowest
key on your keyboard doesn't play the first key on screen, use **MIDI... > Set low key**.

### Akai MPK mini 3 ("25 keys 3")

Pick **25 keys 3**. A short setup asks you to hit pads 1-8 and turn knobs 1-8 (keep the MPK on Bank A with CC and
Prog Change off). Then the pads play this bank (Bank B = the next bank) and the knobs turn the pitch knobs. The mini 3
has no transport buttons - use Space or MIDI Learn other controls for play / stop.

### Akai MPK mini IV ("25 keys IV")

Pick **25 keys IV** in the keyboard box (top bar). The first time, a short **MPK mini IV setup** window opens and asks
you to press each control once: Stop/Play, Record, Overdub, Loop, Undo, pads 1-8 and knobs 1-8 (Skip anything you
don't use). After that:

* The 8 pads play the 8 pads of the bank on screen; **Bank B** on the MPK plays the *next* bank (e.g. A1-A8 + B1-B8).
  Pad notes never play the on-screen keys, so the keys and pads can share a MIDI channel only if you skip the pads step.
* The 8 knobs turn the 8 pitch knobs. Endless knobs are recognised automatically (2 clicks = 1 semitone); you can
  also tick *Knobs are endless* in the setup window.
* Transport buttons toggle: Play again stops, Stop again plays, Record again turns recording off.
* Transport buttons send on the MPK's **DAW / Software Control** port: switch on every *MPK mini IV* input in the
  MIDI window. Links are learned per MIDI channel, so they don't collide with the keys.
* Run the setup again any time with **MPK mini IV setup...** in the MIDI window. Pick **25 keys** to switch the profile off.

### The library

The library is a normal folder: `%APPDATA%\CONTRXL25\Library` (Mac: `~/Library/Application Support/CONTRXL25/Library`), or any folder you pick with
**More... > Use another folder as the library**. Every folder inside is a crate, and folders inside folders are
sub-crates — e.g. `Soul Samples\Artist\Kicks`. Copy samples in with Explorer (Finder) and they appear in the app within a few
seconds (or press **Refresh**). Samples you add inside the app (**Pad > library**, **+ Its chops**, **Bounce > library**)
are written into their crate folder when you press **Save** or close the app. Drag a sample from the list onto a pad,
or drop files / folders from Explorer onto the library to copy them into the selected crate.

### Live recording into tracks

Arm one or more tracks (**R**). A hit goes to the selected armed track if it can take it, otherwise to an armed track
that already plays that pad in this pattern, otherwise to the next armed track that is empty in this pattern — so with
several tracks armed, each pad lands on its own track. Keys played in chop mode stay on one track (each note keeps
its chop). Switch on **Rec all armed** to record every hit onto every armed track at once (layering). Each pattern can use a different pad on the same track.
**L** locks a track so recording never touches it; **S** solos it. The mixer strips have **R** and **L** too.

### VST3 / Audio Unit plug-ins and the mixer

1. Open the **Mixer** tab and click **Plug-ins...** → **Scan all...**. The usual folder
   `C:\Program Files\Common Files\VST3` is scanned (Mac: the VST3 folders, and Audio Units are found through macOS); add your own folders when it asks. The list is remembered.
   **Scanning is safe:** each plug-in is opened in a small helper program (`CONTRXL25_PluginScanner`). A plug-in that
   crashes or hangs (more than 60 s) only stops the helper: it is marked **Failed**, put on the skip list, and the scan
   goes on with the next one.
   **Choose & scan** (second tab, or the **Choose & scan...** button) lists every plug-in on the computer without
   opening any of them. Tick the ones you want (**Tick new**, **Tick failed**, or click rows), then **Scan ticked**.
   **Add a plug-in file...** scans one `.vst3` (Mac: also `.component`) that you pick by hand. **Skip ticked** /
   **Un-skip ticked** decide which plug-ins are never scanned; a failed plug-in can be retried any time.
2. **Instrument on a pad**: in the Mixer click **+ instrument** on a strip (or right-click a pad → *VST instrument...* →
   *Search instruments...*). Type part of the name, maker or type, then Enter (recently used plug-ins come first).
   You can also drag a `.vst3` from Explorer straight onto a pad or a slot. Its editor opens. Now the pad, the keyboard, your MIDI keyboard and the sequencer play it.
   Right-click the slot → *Pad / root key plays note* to choose which note the pad plays.
3. **Effects**: **+ insert** on a pad strip (2 per pad), on Return A / B (2 each, fed by the A / B send knobs) or on the
   Master (4). Click a slot to open its editor; right-click for Bypass / Replace / Remove.
4. **Auto-sample**: right-click the instrument slot (or the pad) → *Auto-sample...*. *Keys* records every key into one
   chopped sample (chop mode: each key plays its own note); *Pads* puts one note on each pad. Or just press **Resample**
   while you play the synth.

Signal flow of a pad: samples + VST instrument → built-in Pad FX → VST inserts → pan → mute / solo → level →
sends A / B → master → master VST inserts → LA-2A → limiter → master volume.

Notes:
* Only **VST3** plug-ins are supported (64-bit). Old VST2 `.dll` plug-ins need Steinberg's discontinued VST2 SDK, which
  can't be included; most makers ship a VST3 version.
* Scanning can't crash the app any more (see above), but once a plug-in is *loaded* it runs inside CONTRXL 25: a buggy
  plug-in can still crash the app while you play it. Save your song before trying new plug-ins.
* Keep `CONTRXL25_PluginScanner` next to the app: inside `CONTRXL 25.app` on a Mac (the build puts it there),
  next to `CONTRXL 25.exe` on Windows. Without it, scans fall back to the old (unsafe) way.
* Pad inserts are not latency-compensated against each other (the master plug-ins' latency is removed from exports).
* Heavy synths on many pads use a lot of CPU — raise the audio buffer to 256–512 if you hear crackles.

---


### Recorded hits land late (or on the step before)?
Open **Sequencer > Rec timing...**, press **Start tap test** and tap any pad or key exactly on each click (8 times).
The app measures the delay of your speakers / headphones / MIDI keyboard and corrects every recording from then on.
Bluetooth headphones are the usual cause - wired headphones or the computer's speakers are tighter.

## 3. What is in the box

```
CONTRXL25/
  CMakeLists.txt          build script (downloads JUCE automatically)
  build_windows.bat       one-click Windows build -> dist\CONTRXL 25.exe
  build_mac.sh            one-step macOS build    -> dist/CONTRXL 25.app
  Build CONTRXL 25.command  double-click in Finder to run build_mac.sh
  Assets/icon.png         app icon
  Source/
    Main.cpp              application + window
    model/Model.*         data model (pads, layers, tracks, patterns, chain, settings)
    engine/Snapshot.*     immutable copy of the project for the audio thread + processed-audio cache
    engine/Engine.*       real-time engine: voices, choke/mute/link groups, sequencer,
                          metronome, master LA-2A + limiter, sampling, offline export render
    engine/Effects.*      per-pad rack: saturation, vinyl, chorus, flanger, scatter, delay, echo, reverb
    engine/Plugins.*      hosted VST3 plug-ins (nodes, rack, safe swap / retire on the audio thread)
    dsp/Offline.*         normalize, reverse, lo-fi, 7 retro modes, WSOLA + tape stretch,
                          transient chops, slice / trim
    dsp/Stems.*           stem separation (vocals / bass / instruments / drums)
    io/SongIO.*           audio decoding, WAV writer, song .zip (web compatible), library, tempo map .mid
    app/Controller.*      every user action, undo/redo (40 steps), clipboard, export jobs, plug-ins, auto-sampler
    app/PluginManager.*   plug-in scanning list and creation (VST3; + Audio Units on macOS)
    app/Platform.h        Windows / macOS words (Ctrl/Cmd, Recycle Bin/Trash...) and the app-data folder
    ui/*                  look & feel, pads, knobs, wheels, keyboard, waveform, sequencer, panels
  Tests/EngineTest.cpp    headless test (cmake -DCONTRXL_BUILD_TESTS=ON, target EngineTest)
  PORTING_NOTES.md        how each part of the web pseudocode maps to the C++ code
```

### How it works (short version)

* The **UI thread** owns the project. Every edit builds a new immutable **Snapshot** and hands it to
  the engine with a lock-free swap. Old snapshots are freed on the UI thread, never on the audio thread.
* The **audio thread** (`Engine::process`) never allocates or locks. Pads, keys and MIDI arrive through
  lock-free FIFOs; recorded hits and MIDI CCs go back the same way.
* The **sequencer runs inside the audio callback** and is sample-accurate (the web version used a
  25 ms timer with look-ahead). Every hit, unquantized event and tempo change lands on its exact sample.
* **Exports use the same engine offline**, so a song `.wav` and the stems sound exactly like playback,
  start on bar 1 beat 1 and have identical lengths.

---

## 4. Differences from the web version

| Area | Web | Desktop |
|---|---|---|
| Reverb | generated impulse response + convolver | Freeverb-style algorithmic reverb (same Size / Damping / Mix controls) |
| Sequencer timing | 25 ms look-ahead timer | sample-accurate in the audio thread |
| Library | browser IndexedDB | a folder (`%APPDATA%\CONTRXL25\Library` or one you choose): crates are folders and sub-folders |
| Audio devices | browser default / setSinkId | full device dialog: WASAPI shared/exclusive, DirectSound; any sample rate (samples are resampled on the fly) |
| Limiter | Web Audio DynamicsCompressor (6 ms look-ahead) | 1.5 ms look-ahead limiter with the same settings; exports remove the look-ahead |
| Pop-out windows | manual chop, sections | **Chop window**, **sequencer** and **keyboard** pop out into their own windows (great on a second screen); positions are remembered |
| VST3 plug-ins + mixer | no | instruments on pads, pad / return / master inserts, mixer, auto-sampler |
| Keys / Chop mode | one mode for the whole app | per pad — chops-to-keys on one pad never affects the others |
| MIDI Learn (map any control) | yes | yes — plus a MIDI window (monitor, links list, notes-from-one-input filter, low key) |

---

## 5. Roadmap / ideas

1. CONTRXL 25 itself as a VST3 plug-in for your DAW (the engine is already a self-contained class).
3. Swing / groove, per-step velocity and probability, pattern names and colours.
4. Neural stem separation (ONNX runtime) as an optional upgrade to the DSP estimate.
5. ASIO driver support (needs Steinberg's free ASIO SDK; set `JUCE_ASIO=1`).

© 2026 Illahtainment. All rights reserved. Built with JUCE (https://juce.com) — JUCE is free to use
for personal projects under its Starter/AGPL terms; check the JUCE licence before selling the app.
VST is a registered trademark of Steinberg Media Technologies GmbH. If you sell the app and mention VST3 support,
follow Steinberg's VST3 usage guidelines (free to sign up at steinberg.net).
