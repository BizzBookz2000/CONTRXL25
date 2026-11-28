CONTRXL 25 - MPC Sample edition  1.0.0
(c) 2026 ILLAHTAINMENT. All rights reserved.
=====================================================================

CONTRXL 25 running on the Akai MPC Sample (firmware 1.4, Developer Mode),
started from the Illah Choice multiboot menu. It does not change anything
inside the MPC: it runs from the SD card, and quitting returns to the menu.


INSTALL
-------
1. Your card already has the multiboot (autorun.sh + the "autorun" folder).
2. Copy this "CONTRXL25" folder into the "mods" folder at the top of the
   card (make "mods" if it is not there):   <card>/mods/CONTRXL25/
3. Put the card in the MPC, switch it on, answer YES to the autorun
   question. "CONTRXL 25" appears in the menu with the CX25 cover.

To update: replace the folder. To remove: delete the folder.
Your songs, samples and settings are kept in <card>/CONTRXL25/ :
   Songs/        song files (.zip) - the SAME files as CONTRXL 25 for
                 Windows, Mac and the web version. Copy them both ways.
   Samples/      put your WAV files here (any folders). The first start
                 adds "CONTRXL Kit" (12 sounds made by CONTRXL).
   Recordings/   every sample you record is saved here too
   Exports/      pattern, song and stem .wav files (24-bit, 44.1 kHz)
   settings.json, contrxl25.log


THE SCREEN
----------
 Top row ........ what the three buttons ABOVE the screen do (B1 B2 B3)
 Red display .... messages, pattern, tempo, play/record state
 Bottom row ..... what K1 K2 K3 do on this page
 The open page's button lights up white.


EVERYWHERE
----------
 PADS ............... play (velocity and pressure sensitive)
 SHIFT + pad ........ select a pad without playing it
 PAD BANK ........... next bank pair: A+B, C+D, E+F, G+H (SHIFT = back)
                      64 pads like CONTRXL: pads 1-8 = first bank,
                      9-16 = second bank. The bank lights show which.
 16 LEVELS .......... key mode:  PADS -> KEYS -> CHOP
                        KEYS: the 16 pads play the selected pad in
                              semitones (pad 1 = as recorded),
                              - / + change the octave
                        CHOP: the 16 pads play its chops,
                              - / + go to chops 17-32 ...
 PLAY / STOP ........ start / stop.  STOP twice = all sounds off
 SHIFT + PLAY ....... song mode on/off
 SEQ RECORD ......... record (count-in when stopped; overdub when playing)
 TAP TEMPO .......... tap the tempo (it blinks on the beat)
 SHIFT + TAP TEMPO .. metronome on/off
 NOTE REPEAT (hold) + pads ... repeat at 1/16 (pressure = velocity)
 SHIFT + NOTE REPEAT ......... repeat rate 1/4 ... 1/32T
 ERASE (hold) + pad .. erase that pad from the pattern
 SHIFT + ERASE ....... undo (press again = redo)
 MUTE (hold) + pad ... mute / unmute a pad
 SAMPLE RECORD ...... record a sample (opens the SAMPLE page)
 MAIN VOLUME ........ output level
 FADER .............. selected pad's level (Settings: or its filter, or master)
 SHIFT + wheel PUSH . menu
 Wheel / - / + ...... scroll and change values; PUSH = choose


PAGES
-----
 Main (PADS) ... the 16 pads on screen + the selected pad
     B1 VEL  B2 CHOKE  B3 GATE    K1 tune  K2 level  K3 filter
     PUSH = MENU
 SEQ ......... sequencer (16 patterns, 1-8 bars, live and step)
     B1 STEP/LIVE: in STEP the pads are the 16 steps of the selected
        pad's track; hit a pad to add (velocity = how hard) or remove.
        Hold a step and hit another = copy the step there.
        ERASE + step pad = clear that step.
     B2 next bar (SHIFT = back)   B3 copy pattern to the next empty one
        SHIFT + B3 = clear the pattern
     K1 length  K2 quantize (1/16 or exact timing)  K3 tempo
     Wheel = pick the track (pad).  - / + = pattern 1-16.
     Live hits keep their exact timing (unless quantize is on).
 LOOP ........ SONG chain: B1 insert, B2 delete, B3 song mode
     K1 pattern, K2 repeats (or - / + and SHIFT + - / +)
     SHIFT + LOOP = loop on/off
 SAMPLE ...... record from LINE IN or the built-in MIC, or RESAMPLE
     what CONTRXL plays. B1 input, B2 input/resample, B3 threshold.
     K1 threshold, K2 input gain, K3 monitor.
     SAMPLE RECORD starts and stops (max 60 s). The take goes on the
     selected pad if it is empty, otherwise on the next empty pad,
     and is saved in CONTRXL25/Recordings.
 CHOP ........ waveform + chops of the selected pad. Pads play chops.
     B1 auto chop: 4 / 8 / 16 equal, transients, none
     B2 split the chop   SHIFT + B2 delete its start marker
     B3 chops to the next empty pads   SHIFT + B3 trim to the chop
     Wheel = pick a chop, K1/K2 move its start, PUSH plays it.
 PAD FX ...... everything about the selected pad
     B1 SOUND: level, pan, tune, filter (LP/BP/HP, cutoff, resonance),
        retro mode (ASR-10, MPC60, MPC3000, SP-1200, SP-202, SP-303,
        SP-404), lo-fi, mute group, link group, key mode, root key
     B2 FX: saturation, vinyl, chorus, flanger, scatter, delay,
        tape echo, reverb (the CONTRXL 25 effects rack)
     B3 TOOLS: normalize, reverse, mono, half / double speed and
        fit to 1, 2 or 4 bars (time-stretch, same pitch), copy/paste
        pad, put the pad on a key, layers (level, tune, remove), clear
     Wheel picks a line, - / + or K1 changes it, PUSH switches / runs.
 KNOB FX ..... MIXER: the 16 pads' levels, meters and pans
     K1 level  K2 pan  K3 master   B1 mute  B2 solo  B3 reset
 SAMPLE SELECT  BROWSER: WAV files on the card (and .zip songs)
     Wheel scrolls (plays each WAV when AUDITION is on)
     PUSH loads it on the selected pad   B2 on the next empty pad
     B3 adds it as a LAYER (up to 8 per pad, they play together)
     - goes up a folder
 MENU (wheel PUSH on the main page)
     Save song / Save as new / Open song / New song / CONTRXL kit,
     Export pattern / song / stems (one .wav per pad, same length -
     they line up in any DAW), Settings, About, Exit CONTRXL 25


MIDI
----
 A USB keyboard (MPC's USB host port), the MIDI IN jack or a computer
 on USB-C play the selected pad like the CONTRXL 25 keyboard:
 KEYS pads are pitched (root key on the PAD FX page), CHOP pads play
 one chop per key, "Keys play pads" plays the pads you put on keys.
 Settings > MIDI notes play: Pads = note 36 is A1, 37 A2 ... (64 pads).
 MIDI Start / Stop / Continue start and stop CONTRXL.


DIFFERENCES FROM THE DESKTOP VERSION
------------------------------------
 - No VST/AU plug-ins and no stem separation (the MPC is too small);
   songs made on the desktop keep those settings when saved here.
 - 44.1 kHz only (firmware 1.4 runs the codec at 44.1 kHz).
 - Song names are SESSION_01, SESSION_02 ... (no keyboard to type).


IF SOMETHING GOES WRONG
-----------------------
 - It quits back to the menu: open CONTRXL25/contrxl25.log on a
   computer and send it over.
 - A song that crashes while opening is skipped at the next start.
 - Hits land late when recording: Settings > Rec timing offset.
