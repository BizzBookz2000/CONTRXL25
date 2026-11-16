# Porting notes — web build 67 → JUCE

Each section of the web pseudocode reference and where it now lives.

| Pseudocode section | Web function(s) | C++ |
|---|---|---|
| 2 Data model | `S`, `pads[64]`, `tracks[]`, `PLEN`, `PBPM`, `chain`, `LIB` | `model/Model.h` (`Project`, `Pad`, `Layer`, `Track`, `PatternData`, `Settings`, `LibItem`) |
| 3.1 Master chain | `makeMaster`, `applyLA` | `Engine::process` (end of block), `Engine::applyLA`, `Compressor` |
| 3.2 Playing a sound | `startVoice`, `chokeVoice`, `releaseVoice` | `Engine::startVoice`, `Engine::renderVoice`, `chokeVoice`, `releaseTid`, `releaseLive` |
| 3.3 Buffer pipeline | `playBuf`, `layBuf`, `rmBuf`, `smBuf`, `lofiBuf` | `SnapshotBuilder::playBuffer / layerBuffer / derive` (cached) |
| 4 Pads, keys, knobs, wheels | `hitPad`, `noteOn`, `noteOff`, `setPitch`, `setBend` | `Controller::hitPad / noteOn / setPitch`, `Engine::noteOn / noteOff`, `ui/Widgets` (`PitchKnob`, `Wheel`, `PadButton`), `ui/KeyboardView` |
| 5.1 Loading | `loadFiles`, `prepBuf` | `Controller::loadFiles`, `io::decodeFile` |
| 5.2 Sampling | `startSampling`, `stopSampling` | `Controller::startSampling / finishCapture`, `Engine::startCapture / runCapture / takeCapture` |
| 5.3 Chops | `segs`, `transientMarkers`, `chopsToPads`, `chopsToKeys`, extract panel | `segments()`, `dsp::transientMarkers`, `Controller::chopsToPads / chopsToKeys / extractChop / assignChopKey`, `ExtractBox` in `ui/Panels.cpp` |
| 5.4 Trim preview | `TR`, `pvGet`, `trimShape`, `trimApply` | `TrimState`, `SnapshotBuilder::previewBuffer`, `dsp::trimShape`, `Controller::trimApply` |
| 5.5 Layers | `layersOf`, `editLayer` | `Pad::layers`, `Controller::editSlot / swapLayerWithMain / removeLayer`, `LayersPanel` |
| 6.1 Normalize, reverse, lo-fi | `normalizeInPlace`, `reversePad`, `lofiBuf` | `dsp::normalized / reversed / lofi` |
| 6.2 Time stretch | `wsolaStretch`, `resampleStretch` | `dsp::wsolaStretch / resampleStretch`, `Controller::applyStretch / matchBpm` (Match BPM now uses the current pattern's tempo — item 2 of the "next steps" list) |
| 6.3 Retro modes | `RETRO`, `retroProcess`, `rbj`, `biq` | `dsp::retroProcess`, `dsp::Biquad` (same profiles and maths) |
| 6.4 Stems | `separateStems`, `smBuf`, `stemApply`, `stemsToPads` | `dsp::separateStems` (JUCE FFT), `dsp::mixDown`, `dsp::StemStore`, `Controller::runSeparate / applyStems / stemsToPads` |
| 7 Effects rack | `modSat … modRev`, `fxBus`, `fxRoute` | `PadFx` in `engine/Effects.cpp` (one rack per pad, allocated on first use) |
| 8.1–8.2 Sequencer | `play`, `sched`, `scheduleStep` | `Engine::startPlay / advanceSequencer / scheduleStep` — runs per sample inside the audio callback |
| 8.3 Live recording | `recordHit` | `Engine::recordHit` (exact position, latency compensated) → `Controller::applyRecHit` (armed-track rules) |
| 8.4 Patterns / chain / editing | `copyPattern`, `duplicatePattern`, `chain*`, selection tools | `Controller::copyPattern / pastePattern / duplicatePattern / chain* / copySteps / pasteSteps / transposeSteps` |
| 9 Export | `renderLanes`, `masterLatency`, `trimTail`, stems zip, `tempoMapMid` | `Engine::renderPlan` (offline engine instance), `Controller::exportSongWav / exportStems`, `io::tempoMapMid / tempoMapTxt` |
| 10 Songs + library | `buildSongZip`, `loadSongFile`, `makeZip`, `unzip`, IndexedDB | `io::saveSong / loadSong` (JUCE `ZipFile`, stored entries → web compatible), `io::saveLibrary / loadLibrary` |
| 11 MIDI | `onMidi`, `DEFCC`, `ccPress` | `Engine::handleMidi` (audio thread, sample-accurate notes), `Controller::handleEvent` (transport CC debounce) |
| UNDO / REDO | `pushUndo`, `snap`, `restoreSnap` | `Controller::pushUndo / undo / redo` — whole-`Project` snapshots; audio is shared by reference so this is cheap |

## Threading rules (keep these when you extend the code)

1. Only the message thread touches `Controller::P`. After changing it call `ctl.changed()`
   (publishes a snapshot and refreshes the UI).
2. The audio thread only reads `Snapshot` objects. Anything it needs must be put there by
   `SnapshotBuilder::build`.
3. Talk to the engine with `engine.push (Command)`; read back with `engine.pop (EngineEvent)`
   (done for you in `Controller::timerCallback`).
4. Never free audio on the audio thread: `SampleData` is reference counted and the controller keeps
   every snapshot in `releasePool` until the engine no longer uses it.
5. Heavy offline work (stems, stretch, export) runs in `runJob(...)` with a progress window;
   the work lambda must only use copies, and its completion lambda applies the result to `P`.

## Adding a new pad effect (example)

1. Add parameters to `FxParams` (`model/Model.h`) and to the `FXF` / `FXB` tables in `Model.cpp`
   (song files pick them up automatically).
2. Add the DSP to `PadFx::process` (`engine/Effects.cpp`) and a module bit for its state reset.
3. Add a card in `EffectsPanel` (`ui/Panels.cpp`) — one line in the `defs` table.

## Testing

`cmake -B build -DCONTRXL_BUILD_TESTS=ON` then build the `EngineTest` target and run it.
It renders a small song offline and checks timing (first hit on sample 0, pattern 2 on the exact
sample at its own tempo), the mix vs stem alignment, all retro modes, WSOLA lengths, stem
reconstruction, song save/load round trip, the tempo map, and the live paths (pads, keys, MIDI,
transport CCs, live recording position, resampling, gate).
