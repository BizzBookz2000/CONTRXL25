// CONTRXL 25 - files: audio decoding, WAV encoding, song .zip (compatible with the
// web version: project.json + samples/*.wav, stored zip), tempo maps.
#pragma once
#include "model/Model.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace cx::io
{
juce::var pluginSlotToVar (const PluginSlot& s);
PluginSlot pluginSlotFromVar (const juce::var& v);

juce::AudioFormatManager& formats();                 // WAV, AIFF, FLAC, OGG, MP3 (+ WMA/M4A on Windows)
juce::String audioWildcard();                        // "*.wav;*.aif;..."
SamplePtr decodeFile (const juce::File& f, juce::String* error = nullptr);
SamplePtr decodeMemory (const void* data, size_t size, juce::String* error = nullptr);

// WAV writer: bits = 16, 24 or 32 (32 = float). Always writes 2 channels when stereo=true.
juce::MemoryBlock encodeWav (const juce::AudioBuffer<float>& b, double sampleRate, int bits);
bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sampleRate, int bits);

juce::String safeName (const juce::String& s);

// songs
bool saveSong (const Project& p, const juce::File& zipFile, juce::String& error);
bool loadSong (const juce::File& zipFile, Project& out, juce::String& message);

// sample library kept on disk between sessions (the web version used the browser's IndexedDB):
//   <dir>/library.json + <dir>/crates/<crate>/<name>.wav
bool saveLibrary (const juce::File& dir, const std::vector<LibItem>& lib, const std::vector<juce::String>& crates);
void loadLibrary (const juce::File& dir, std::vector<LibItem>& lib, std::vector<juce::String>& crates);

// tempo map for a DAW (standard MIDI file, format 0, 480 PPQ)
struct TempoChange { int step; float bpm; };
std::vector<TempoChange> planTempos (const Project& p, const std::vector<int>& plan);
juce::MemoryBlock tempoMapMid (const Project& p, const std::vector<int>& plan);
juce::String tempoMapTxt (const Project& p, const std::vector<int>& plan);
} // namespace cx::io
