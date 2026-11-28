# MPC Sample (AC50) control surface - map used by CONTRXL 25

Facts are from public measurements (keremimo/sample-base docs/HARDWARE.md, AGPL - only facts used, no code).

* Surface: raw MIDI, 4 Mbaud UART3. Pads: channel 10 notes, pads 1-16 = 37 36 42 82 40 38 46 44 48 47 45 43 49 55 51 53
  (pad 1 bottom left), note-on velocity, polyphonic pressure on the same note.
* Buttons: channel 1 notes (on 127 / off 0): Knob FX 0, Pad FX 1, Sample 6, SEQ 7, Erase 9, Note Repeat 11,
  above-screen 12 13 14, Pad Bank 35, Sample Select 36, 16 Levels 40, Mute 43, Shift 49, Tap Tempo 53, + 54, - 55,
  Sample Record 73, Loop 74, Chop 75, Seq Record 80, Stop 81, Play 82, wheel push 111.
* Controls: channel 1 CC: K1-K3 16-18, main volume 19, fader 20 (absolute 0-127), wheel 100 (relative 1 / 127).
  `F0 47 7F 5C 42 00 00 F7` asks the surface for the current knob/fader positions.
* Lights: pads `F0 47 7F 5C 65 00 04 I R G B F7` (I = pad - 1, 16 = fader light, R G B 0-127);
  bank A-H channel-1 CC 57-64; buttons channel-1 CC = button note (0 off, 1 dim, 2 on, 3 dim red, 4 red);
  level meters CC 83 / 84 = 5-LED mask.
* Audio: ALSA card id `I2S`, stereo S32_LE, 128-frame periods; firmware 1.4 holds 44.1 kHz.
  Mixer switches `TRS Switch` (line in), `Builtin Microphone Switch` (never both), `Builtin Speaker Switch`.
* Screen: DRM `/dev/dri/card0`, stock st7796s driver, landscape 320x240, XRGB8888 dumb buffer, flushed with DIRTYFB.
