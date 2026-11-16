# Changelog

## 0.76.1 (macOS fix)
- **Fixed: on the Mac every plug-in failed to scan** ("no usable plug-in inside"). The scanner helper waited for a
  macOS message loop that a helper program doesn't have, so it never opened the plug-in. VST3 and Audio Units are
  now opened directly; Audio Unit v3 plug-ins get a proper macOS message loop.
  After updating: Mixer > Plug-ins... > Choose & scan > **Tick failed** > **Scan ticked** to rescan the ones that
  were wrongly marked as failed.
- `build_mac.sh` now also installs the app into **Applications** (and opens it in Finder), and warns when the
  project is on a USB / external drive (build from the Mac's own disk instead).

## 0.76.0
- **Plug-in scanning can no longer crash the app** (the macOS crash when scanning).
  - Every plug-in is opened by a separate helper, `CONTRXL25_PluginScanner`, one plug-in at a time. If a plug-in
    crashes, or doesn't answer within the time-out (60 s by default), only the helper stops: the plug-in is marked
    *Failed*, added to the skip list, and the scan carries on. This also covers JUCE's own *Scan all* dialog and
    plug-in files dropped on pads / slots.
  - The helper is built with the app and copied inside `CONTRXL 25.app` (Mac) or next to the `.exe` (Windows;
    `build_windows.bat` copies it to `dist`).
- **Choose & scan** (new tab in Plug-ins...): lists every VST3 / Audio Unit on the computer without opening them;
  tick the ones to scan (Tick new / Tick failed / Tick all / click a row), Scan ticked, Stop, Skip / Un-skip ticked,
  a time-out choice (15 s - 5 min), and **Add a plug-in file...** to scan a single `.vst3` / `.component` by hand.
- Dropping a plug-in that already failed shows "skipped - retry it in Choose & scan" instead of trying again.
- `ScanTest` (with `-DCONTRXL_BUILD_TESTS=ON`) checks that crashing, hanging and empty plug-ins are skipped safely.

## 0.75.1 (macOS)
- **Builds and runs on macOS** (Intel and Apple silicon, macOS 10.13+ to run, 10.15.4+ to build) from the same code as Windows.
  - `build_mac.sh` / `Build CONTRXL 25.command`: one-step build that fetches CMake and JUCE 8.0.12 if needed,
    puts `CONTRXL 25.app` in `dist/` and signs it for this Mac.
  - **Audio Unit** instruments and effects next to VST3 (scan both from Mixer > Plug-ins...; `.component` files can be
    dropped on pads and slots like `.vst3`).
  - Mac wording everywhere: Cmd instead of Ctrl, Option to copy when dragging, Trash, Finder.
  - Library and plug-in scan data live in `~/Library/Application Support/CONTRXL25`.
  - Dark native title bar on macOS; songs, samples and audio files can be dropped on the Dock icon / opened with the app.
  - Builds warning-free with Clang (Xcode's compiler): explicit braces in the sequencer step drawing (no behaviour change).

## 0.75.0
- Live recording with several armed tracks:
  - A hit now goes to the armed track that plays that pad (e.g. track 2 = A2), even if another armed track is
    selected. A pad with no armed track of its own goes to the selected armed track if it is empty, else the next
    empty armed track - which then keeps that pad.
  - Playing the keys in chop mode keeps the whole performance on ONE track: every note remembers its own chop
    (shown as c1, c2... on the steps). Copy / paste / move steps and song files keep it.
  - New **Rec all armed** switch (sequencer): every armed track records every hit, each with its own sound (layering).

## 0.74.0
- **Library = a folder on your PC, crates = folders, sub-crates = sub-folders** (e.g. Soul Samples > Artist > Kicks).
  - Files you copy into those folders with Explorer show up by themselves (checked every few seconds while the
    library is on screen and whenever you come back to the app), or press **Refresh**.
  - **Save** writes the samples you added in the app (pad > library, chops, bounces) into their crate folders;
    the button shows how many are waiting. Closing the app still saves them too.
  - Choosing a crate shows its sub-crates' samples too. **+ Sub-crate** makes a folder inside the selected crate.
    Rename / delete crate rename / remove the real folders (deleting moves the samples up one level).
  - Drop files or whole folders from Explorer onto the library to copy them into the selected crate; drag a sample
    from the list onto a pad to load it. Remove sends the file to the Recycle Bin.
  - **More... > Use another folder as the library** points CONTRXL 25 at a sample folder you already have.
  - Samples load when you use them, so a big library opens fast. Songs no longer carry a copy of the whole library.
  - The old library is converted to plain folders automatically the first time.
- **Pad > library** button (next to Bounce > library): adds the selected pad to the selected crate (top level if
  "All crates" is selected).
- **Live recording goes into any armed track**, in every pattern: the selected armed track first, then an armed track
  that already plays that pad in this pattern, then an armed track that is still empty in this pattern. A track can
  now play a different pad in each pattern (pattern 2 no longer has to use the track's pattern-1 pad).
- **Lock (L)** next to the arm button (R) on every track: live recording never writes into a locked track.
  **Solo (S)** per track. Both also in the sequencer toolbar; saved in songs.
- Mixer strips have **R** (arm the track that plays this pad - an empty track is picked if none does) and **L** (lock it).
- MIDI: a control can be linked from **several keyboards** (learning on the M32 no longer removes the A25's link);
  program-change buttons can be learned; the "notes from one input" filter no longer blocks learning or learned
  buttons from another keyboard; the MIDI window lists every MIDI input with an on/off switch (Komplete Kontrol
  keyboards send their transport on a separate "DAW" port).

## 0.73.0
- **Plug-in browser with search**: click an empty plug-in slot (or right-click a slot / pad > Search...) and type part of a
  name, maker or type ("serum", "native", "reverb", "drum"). Arrow keys + Enter or double-click loads it and opens its editor.
  Recently used plug-ins are listed first. Instrument slots list instruments, insert slots list effects.
- **Drop a .vst3** file or folder from Explorer onto a pad (becomes its instrument) or onto a mixer slot: it is added
  to your plug-in list and loaded, no scan needed.
- Menus still offer the by-maker lists.

## 0.72.0
- **VST3 plug-ins**: instruments and effects, everywhere:
  - **VST instrument on any pad**: the pad, the keyboard (chromatic, with pitch bend, mod wheel, sustain and aftertouch
    from your MIDI keyboard), the sequencer and live recording all play it. Notes last as long as you hold the pad / key;
    sequencer notes last one step. Choose which note the pad / root key plays.
  - **2 VST inserts per pad**, after the built-in pad FX.
  - **2 send / return buses**, each with 2 VST inserts.
  - **4 VST inserts on the master**, before the LA-2A and limiter.
  - Plug-in editors open in their own (dark) windows. Bypass, replace, remove; Ctrl+Z brings a removed plug-in back
    with its sound. Plug-in settings are saved inside songs (a missing plug-in is kept and shown with a "?").
  - **Plug-ins...** window: scan for VST3 plug-ins (the list is remembered; a plug-in that crashes the scan is skipped next time).
- **Mixer** tab: a channel strip per pad (8 per bank, bank buttons A-H) with instrument slot, Pad FX, 2 inserts, send A / B,
  pan, mute, solo, level fader and meter; strips for return A / B and the master with their inserts. Mixer faders and
  knobs can be MIDI-learned (they follow the bank, like an MPC).
- **Auto-sample**: plays a pad's VST instrument note by note and records it: one sample with a chop per key (chop mode,
  every key plays its own note, like an MPC keygroup), or one note per pad (drum kits). Choose notes, step, hold time,
  release tail and velocity.
- Exports, stems and bounces render the plug-ins too (offline copies, so playback is not disturbed); the master
  plug-ins' latency is removed so exports still start on the first sample. Stems are rendered without the master inserts.
- Resample captures everything, plug-ins included.

## 0.71.0
- **MIDI Learn** (top bar): click MIDI Learn, click any pad, key, pitch knob, bank, button or slider (in the main window
  or any pop-out window), then press / move the control on your controller. Linked controls show a green outline and
  their note / CC. Works with notes and CCs; knobs and sliders follow a CC smoothly; CC buttons fire once per press.
  Learned links win over the built-in mappings. Esc cancels the armed control, Esc again (or the button) ends learning.
  Links are saved and come back next time.
- **MIDI window** (MIDI... in the top bar): last MIDI message and which device sent it (a MIDI monitor to find out what
  your transport buttons really send), learned links list (Remove / Clear all), "Notes & pitch bend from" one input
  (so a pad controller or fader box can sit next to the keyboard), Set / Reset low key, Follow keyboard octave buttons,
  Built-in transport CCs on/off, Audio / MIDI devices.
- From the web version: library search, sort (newest / name / length), Play (preview), Onto pads (several samples),
  + Its chops, Import files / folder (sub-folders become crates), Rename / Delete / Save crate, Back up / Restore library;
  Bounce > pad and Bounce > library; Reset keys (Extract box and Chop window); Layers + From pad; sequencer Mute,
  Select track, Clear chain and drag-to-move selected steps (Ctrl/Alt = copy); time stretch Half speed / Double speed;
  drag a chop from the sample window onto a pad; Shift+double-click a pad to load over it.
- Remembered between sessions: keyboard shown / hidden, MIDI settings and links.

## 0.70.0
- The app now fits any screen: **Size** menu in the top bar (Fit = automatic, or 60-125%), and the window scrolls
  (mouse wheel / scroll bars) whenever it is smaller than the layout. The window can be resized freely.
- **Pop out section**: the whole bottom section (sequencer, sound, effects, layers & stems, song & export) pops out
  as one window; the main window then gives the pads and keyboard the space.
- **Trim** in the Chop window (Trim / Apply / Cancel, fade in, fade out, gain).
- Black window frame (dark title bar on Windows 10/11; pure black caption on Windows 11), pop-out windows too.
- Big count-in overlay over the whole window again (count-in, recording and sampling count-ins), like the web version.

## 0.69.0
- Double-click the sample window to load a sample (Fit button resets the zoom).
- New **Chop window**: pops the sample out into its own big window for manual chopping (in-place Manual chop still works).
- **Pop out / Dock** for the sequencer and the keyboard (positions and popped state are remembered).
- Red OLED display, like the web version.
- Transport: PLAY is unlit when stopped, green while playing, red while playing + recording. LOOP/METRO use the hardware style.
- Keys / Chop mode is now **per pad**: Chops > keys only affects that pad; loading a new sample puts its pad back in Keys mode.
- Live recording only writes into **armed** tracks: the selected track first (if armed), else the only armed track,
  else an armed track that already plays that pad, else an empty armed track. A track holding another pad is never re-assigned.
  The display shows which track each hit went to.
- JUCE 8.0.12; build script works with Visual Studio 2019 / 2022 / 2026 and downloads JUCE itself.

## 0.68.0
- First desktop (JUCE) release, ported from web build 67.
