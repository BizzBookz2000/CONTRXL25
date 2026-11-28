# CONTRXL 25 - MPC Sample edition

CONTRXL 25 rebuilt as a mod for the Akai MPC Sample (firmware 1.4.0.8, Developer Mode),
started from the ILLAHTAINMENT "Illah Choice" multiboot menu (`<card>/mods/CONTRXL25`).
(c) 2026 ILLAHTAINMENT. All rights reserved.

The user guide is `docs/README.txt` (it is copied into the mod folder).

## What it is

One static Linux arm64 program (`contrxl25`, C++17, no other libraries on the MPC), written for
this mod. The sound engine, retro modes, effects rack and song format are ported from the CONTRXL 25
desktop app, so pads sound the same and song `.zip` files move between the MPC, Windows, Mac and the web.

| Part | Files |
|---|---|
| Song model + song files (`project.json` + `samples/*.wav` in a zip) | `src/model.*`, `src/json.*`, `src/fileio.*` |
| Sound: voices, layers, chops, mute/link groups, sequencer, note repeat, sampling, export | `src/engine.*` |
| DSP: retro modes, lo-fi, time-stretch, transients, pad FX rack, reverb | `src/dsp.*` |
| Screens, controls, lights | `src/app.cpp`, `src/app_draw.cpp`, `src/gfx.*` |
| MPC hardware: DRM screen, ALSA (tinyalsa) audio, raw MIDI surface + external MIDI | `src/hw_mpc.cpp` |
| Simulator / self-test / benchmark | `src/main.cpp` |
| Fonts + ILLAHTAINMENT logo + menu cover (generated) | `tools/gen_assets.py` -> `src/assets.cpp`, `mod/cover.rgb565` |

## Build (Ubuntu / Debian)

    sudo apt install g++ g++-aarch64-linux-gnu python3-pil qemu-user-static zip
    make assets      # only after changing the logo / fonts
    make sim         # build/contrxl25-sim  (runs on the computer)
    make mpc         # build/contrxl25      (the MPC Sample, static arm64)
    make package     # dist/CONTRXL25-MPC-Sample-<version>.zip  (mods/CONTRXL25)

Checks without an MPC:

    build/contrxl25-sim --sim test/tour.txt --card /tmp/card --out /tmp/shots   # screenshots + LED states + audio
    python3 test/sheet.py /tmp/shots sheet.png 01_main 04_seq ...
    qemu-aarch64-static build/contrxl25 --selftest /tmp/t.wav                    # the arm64 build renders + song round trip
    qemu-aarch64-static build/contrxl25 --bench                                  # heaviest FX load

## On the MPC

`mod/autorun.sh` copies the program to `/run/contrxl25` (the card is mounted noexec) and starts it in the
background with `--card <card root>`; Illah Choice waits for the process named in `watch.txt` to end and then
shows its menu again. The stock app is already stopped by the multiboot session.

Hardware facts used (public interface facts; see `docs/HARDWARE-NOTES.md`): screen `/dev/dri/card0` (st7796s,
320x240, needs DIRTYFB), audio ALSA card `I2S` (AK4558, S32 stereo, 44.1 kHz only on 1.4), control surface =
raw MIDI on the card behind `/dev/snd/by-path/platform-fe670000.serial`, input routes `TRS Switch` /
`Builtin Microphone Switch`.

Third-party: tinyalsa (BSD, `third_party/tinyalsa/NOTICE`). DejaVu fonts (rendered into `assets.cpp`, free licence).
