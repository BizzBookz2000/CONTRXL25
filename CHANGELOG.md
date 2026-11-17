# Changelog

## 0.84.4
- **Right-click a step to clear it** - the penciled hit and any live hits on it (the dots). Right-click and drag to
  clear several steps; only the steps you touch change. A plain click is back to adding / removing a penciled hit
  and no longer clears live hits. (Mac: Ctrl+click works as a right-click.) Ctrl/Cmd+Z undoes a whole drag.
- **Copy a step onto other steps**: hold **Option** (Mac) or **Ctrl / Alt** (Windows), press on a step and drag over
  the steps that should get it - each one becomes a copy (penciled hit, pitch, chop and live hits with their exact
  timing). Works across tracks too. One undo step per drag.
- Select tool Copy / Paste and drag-to-move now carry live hits as well (they used to drop them), and pasting onto
  steps that already have live hits replaces them instead of doubling them.

## 0.84.3
- Clearing live hits from a step (changed to a right-click in 0.84.4).

## 0.84.2
- **Threshold is back in the sample window**: **Threshold** on/off and its **Level** sit right above Load / Sample /
  Resample, next to the level meter. With Threshold on, Sample waits until the input rises above the level (the
  blue line on the meter). Drag on the meter to move the line. Same setting as Sound > Sampling.
- **Rec timing...** (sequencer, next to Rec all armed) - for recorded hits that land a step late, or on the step
  before (e.g. hitting beat 1 lands on 16 or on 2). Sound and MIDI can reach you later than the computer reports -
  Bluetooth headphones / speakers add 150-250 ms, about one step at 84 BPM. Press **Start tap test** and tap any
  pad or key exactly on each click, 8 times: the app measures how late the taps arrive and moves every recorded hit
  back by that much. A stray tap doesn't count. You can also set the correction by hand (-300 to +300 ms) or
  **Reset** it. Saved for this computer (not in songs), so redo it if you change headphones / audio device.
- `TimingTest` (with `-DCONTRXL_BUILD_TESTS=ON`) checks that a hit made when the click is heard lands on that step.

## 0.84.1 (macOS edition of 0.84.0)
Everything in 0.84.0 (pads on keys, sample level meter, Sample into the next empty pad, iPad code, MPK mini
support...) plus the Mac work, which is now part of the shared code:
- **Builds and runs on macOS** (Intel and Apple silicon; runs on macOS 10.13+, builds on 10.15.4+):
  `build_mac.sh` / `Build CONTRXL 25.command` fetch CMake and JUCE 8.0.12 if needed, sign the app for this Mac,
  put it in `dist/` and install it in **Applications**. A warning appears if the project is on a USB drive.
- **Audio Unit** instruments and effects next to VST3; `.component` files can be dropped on pads and slots.
- **Plug-in scanning can't crash the app**: every plug-in is opened by a separate helper,
  `CONTRXL25_PluginScanner`, one at a time. A plug-in that crashes or hangs (time-out 60 s) is marked *Failed* and
  skipped and the scan goes on. Covers *Scan all*, files dropped on slots, and JUCE's scan dialog. Windows too
  (`build_windows.bat` copies the helper into `dist`). Not used on the iPad.
- **Choose & scan** tab in Plug-ins...: list every VST3 / Audio Unit without opening them, tick the ones to scan
  (Tick new / Tick failed / Tick all), Scan ticked, Stop, Skip / Un-skip, time-out choice, Add a plug-in file...
- Mac fix carried over from 0.76.1: Audio Units and VST3s are opened directly in the helper (every scan failed
  before); AUv3 gets a proper macOS message loop.
- Mac wording (Cmd, Option to copy, Trash, Finder), data in `~/Library/Application Support/CONTRXL25`, dark title bar.

## 0.84.0
- **Pads on keys** (Windows, iPad and web): a third key mode, **PADS** ("a pad on each key"), next to KEYS and
  CHOP. Each key plays a whole pad at its own pitch - like a drum kit across the keyboard - and records onto the
  sequencer as that pad.
  - **Pads > keys** (under the mode buttons, and in a pad's menu): key 1 = the first pad that has a sound, key 2
    the next... 8 pads fill 8 keys, 16 fill 16, up to as many keys as your keyboard has (25, 32, or your own size).
    Empty pads are skipped.
  - **Drag a pad onto a key** to put it there (Windows/iPad: drag the pad; web: hold the pad, then drag). Dragging
    a chop out of the waveform onto a key puts that chop on that key.
  - **Put on a key...** in a pad's menu (right-click / hold): then press the key - on screen, QWERTY or MIDI.
  - Keys show which pad they play; moving / swapping pads keeps each key on the same sound. KEYS or CHOP switch
    back to the selected pad (the pad map is kept for PADS); **Clear keys** clears it. Saved in songs; undo works.

## 0.83.0
- **Sample never records over a sample**: if the selected pad already has a sound (a sample, layers or an
  instrument), Sample records into the next empty pad - on through the banks, then round from A1 - and selects it.
  An empty selected pad is used as before. Resample does the same but keeps your selected pad selected (you may be
  playing it). If all 64 pads are in use it says so and doesn't record. (Windows, iPad and web.)

## 0.82.0
- **Sample level meter** (Windows, iPad and the web version): left / right bars from -60 to 0 dB (green to -12,
  yellow to -3, red above), a peak mark that holds for 1.5 s with its dB value, and a **CLIP** light that stays on
  until you click / tap the meter. It shows the audio input all the time, so you can set the level before you
  press Sample; while resampling it switches to **OUT** (what you hear). The blue line is the threshold level when
  Threshold is on. It sits right above Load / Sample / Resample in the sample window, and bigger in Sound > Sampling.
  "No audio input" shows when the audio device has no input. Web version: **Monitor** turns the input meter on
  before sampling (the browser asks for input permission once).

## 0.81.0 - iPad
- **iPad version**: `CONTRXL25_iPad.jucer` + a ready Xcode project in `Builds/iOS` (see README_iPad.md).
  Same source code as Windows, with the iPad differences in `Source/app/Platform.h` (`CX_MOBILE`):
  - AUv3 plug-ins instead of VST3 (they open in the background; offline renders - bounce, export, stems,
    auto-sample - first get their own copies of the AUv3s ready, then run).
  - Hold a finger on anything that has a right-click menu to open it.
  - Library, songs and exports live in the app's folder, visible in the Files app (On My iPad > CONTRXL 25).
  - Bluetooth MIDI pairing button in the MIDI window. Full-screen landscape, touch-sized window title bars.
  - Library, settings and an autosave of an unsaved song are written when the app goes to the background.
- Windows: plug-ins that need asynchronous loading are handled the same way (none of the usual VST3s do).

## 0.80.0
- MPK mini 3 pads felt late and "stuck": their pressure (aftertouch) sends a stream of messages while a pad is held,
  and each one was reported to the screen - thousands per second filled the event queue, so pad selections and
  lights arrived late or got lost. Now streams (pressure, knobs, wheels) are reported at most every 40 ms, pad
  pressure on the pad channel is ignored (it was also being sent to the keys' VST instrument), and a burst of pad
  hits re-selects the pad once per screen update instead of once per hit.

## 0.79.0
- **25 keys 3** (Akai MPK mini 3) in the keyboard box: its 8 pads play the pads (Bank B = the next bank) and its
  8 knobs turn the pitch knobs. A short setup (pads + knobs, no transport - the mini 3 has none) opens the first
  time; run it again with **MPK setup...** in the MIDI window. The joystick bends pitch / sends mod as before.
- **Your own keyboard...** in the keyboard box: type how many keys your keyboard has (25 to 88). The on-screen
  keyboard, MIDI, chop mode and auto-sampling follow it, and the **chop to** buttons get one more button with
  that number (e.g. 49 = 49 equal chops, one per key). Bigger keyboards start lower (49 / 61 keys at C2, 88 at A0);
  MIDI... > Set low key fixes any offset. The computer keyboard still plays the first 32 keys.
- Loading a song keeps the keyboard you play on (it is your hardware, not part of the song).

## 0.78.0
- Transport toggling also works with buttons that **latch** - the ones that send "on" at the first push and "off" at
  the second push (the MPK mini IV's transport buttons in some modes). The app tells them apart from ordinary push
  buttons by itself: a quick release = push button (only presses count), a release that comes at the next push =
  latching button (the "off" is the second press).

## 0.77.0
- Controller transport buttons toggle: **Play** again = stop, **Stop** again = play, **Record** again = recording off.
  Works for MIDI Learn links to the on-screen Play / Stop / Rec, the MPK setup, built-in transport CCs and MMC /
  MIDI Start-Stop. One press that arrives twice (e.g. a note and an MMC message) counts once. (Mouse clicks on the
  on-screen buttons work as before.)
- Pitch knobs from a controller reach every value in between again: endless knobs (like the MPK mini IV's, which
  send 1 / 127 "steps") are recognised automatically after two clicks - 2 clicks = 1 semitone - instead of jumping
  to -24 / +24. Absolute knobs work as before. Applies to the MPK profile and to knobs linked with MIDI Learn.

## 0.76.0
- **25 keys IV** (Akai MPK mini IV) in the keyboard box: a guided setup links Stop/Play, Record, Overdub, Loop and
  Undo, pads 1-8 and knobs 1-8 with one press each (or Skip). Pads play this bank, Bank B the next bank; knobs turn
  the pitch knobs (absolute, or endless "Rel" mode). Re-run it from the MIDI window (**MPK mini IV setup...**).
- MIDI Learn links now remember the MIDI channel ("Note 60 ch10"), so two keyboards or a keyboard's keys and its
  buttons can't clash. Older links (any channel) keep working.
- Learned links can run actions directly (Play/Stop toggle, Record, Loop, Undo...).
- build_windows.bat: no longer stops on tar's harmless "Can't restore time" messages when unpacking JUCE (PowerShell
  fallback if something is really missing), and fixes files dated in the future, which made Ninja loop with
  "build.ninja still dirty after 100 tries".

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
