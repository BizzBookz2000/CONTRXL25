# CONTRXL 25 — desktop edition (JUCE / C++)

A native Windows (and Mac/Linux) rebuild of the CONTRXL 25 web instrument: 64 pads in 8 banks,
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
| Path too long errors | Move the folder to something short like `C:\dev\CONTRXL25`. |
| Build worked but no sound | Click **Audio / MIDI** in the app and pick your output device. "Windows Audio (Exclusive Mode)" gives the lowest latency. |

---

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
| `Ctrl+Z`, `Ctrl+Shift+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+C`, `Ctrl+V` | copy / paste pad (or selected steps with the Select tool) |
| `Ctrl+S`, `Ctrl+O` | save / load song |
| `Delete` | clear selected steps |
| `Esc` | close the chop window / cancel "pick a pad / key" / leave MIDI Learn |

Mouse: double-click the sample window to load a sample; **Chop window** pops the sample out for chopping
(Esc closes it); the sequencer and keyboard have **Pop out / Dock** buttons; drag a pad onto another to move it (hold `Ctrl` to copy); right-click a pad for its menu;
double-click an empty pad to load (Shift+double-click any pad); drag a chop region from the sample window onto a pad to copy it there;
with the sequencer's Select tool, drag selected steps to move them (hold `Ctrl` to copy); mouse wheel on the waveform zooms (Shift+wheel scrolls);
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

### The library

The library is a normal folder: `%APPDATA%\CONTRXL25\Library`, or any folder you pick with
**More... > Use another folder as the library**. Every folder inside is a crate, and folders inside folders are
sub-crates — e.g. `Soul Samples\Artist\Kicks`. Copy samples in with Explorer and they appear in the app within a few
seconds (or press **Refresh**). Samples you add inside the app (**Pad > library**, **+ Its chops**, **Bounce > library**)
are written into their crate folder when you press **Save** or close the app. Drag a sample from the list onto a pad,
or drop files / folders from Explorer onto the library to copy them into the selected crate.

### Live recording into tracks

Arm one or more tracks (**R**). A hit goes to the selected armed track if it can take it, otherwise to an armed track
that already plays that pad in this pattern, otherwise to the next armed track that is empty in this pattern — so with
several tracks armed, each pad lands on its own track. Keys played in chop mode stay on one track (each note keeps
its chop). Switch on **Rec all armed** to record every hit onto every armed track at once (layering). Each pattern can use a different pad on the same track.
**L** locks a track so recording never touches it; **S** solos it. The mixer strips have **R** and **L** too.

### VST3 plug-ins and the mixer

1. Open the **Mixer** tab and click **Plug-ins...** → **Scan for VST3 plug-ins**. The usual folder
   `C:\Program Files\Common Files\VST3` is scanned; add your own folders when it asks. The list is remembered.
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
* Plug-ins run inside CONTRXL 25: a buggy plug-in can crash the app. Save your song before trying new plug-ins.
* Pad inserts are not latency-compensated against each other (the master plug-ins' latency is removed from exports).
* Heavy synths on many pads use a lot of CPU — raise the audio buffer to 256–512 if you hear crackles.

---

## 3. What is in the box

```
CONTRXL25/
  CMakeLists.txt          build script (downloads JUCE automatically)
  build_windows.bat       one-click Windows build -> dist\CONTRXL 25.exe
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
    app/PluginManager.*   VST3 scanning list and plug-in creation
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
