// CONTRXL 25 - hosted plug-ins (VST3 instruments and effects) inside the real-time engine.
//
// A PluginNode wraps one juce::AudioProcessor (a VST3 instance, or an internal processor in the tests).
// The engine's rack holds raw atomic pointers to nodes. The UI thread creates and prepares a node,
// swaps it into a slot, and hands the old node back to the engine, which deletes it only once the
// audio thread can no longer be using it (two finished audio blocks later).
#pragma once
#include "model/Model.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace cx
{
// where a plug-in lives
struct SlotId
{
    enum Kind : int { Inst = 0, PadFx, Master, Send };
    int kind = Inst, a = 0, b = 0;    // Inst: a = pad; PadFx: a = pad, b = insert; Master: b = insert; Send: a = bus, b = insert
    bool operator== (const SlotId& o) const { return kind == o.kind && a == o.a && b == o.b; }
    bool operator< (const SlotId& o) const { return std::tie (kind, a, b) < std::tie (o.kind, o.a, o.b); }
    juce::String label() const;
    static std::vector<SlotId> all();
};

struct PluginNode
{
    explicit PluginNode (std::unique_ptr<juce::AudioProcessor> p, int instanceId, bool isInstrument);
    ~PluginNode();

    std::unique_ptr<juce::AudioProcessor> proc;
    int iid = 0;
    bool instrument = false;
    std::atomic<bool> bypass { false };
    juce::String name;

    // message thread (audio stopped or node not yet in the rack)
    void prepare (double sampleRate, int maxBlock, bool nonRealtime);
    int latency() const { return proc ? proc->getLatencySamples() : 0; }
    juce::String getState() const;                 // base64
    void setState (const juce::String& base64);

    // audio thread
    void addNote (int note, int vel, int offset);   // vel 0 = note off
    void addMidi (const juce::MidiMessage& m, int offset);
    void allNotesOff (int offset);
    // instruments: render and ADD into L/R. effects: process L/R in place.
    void process (float* L, float* R, int n) noexcept;
    bool hasNotes() const { return heldCount > 0; }
    int quietSamples = 0;

private:
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    int numIn = 2, numOut = 2, maxN = 512;
    bool prepared = false;
    std::array<juce::uint8, 128> held {};
    int heldCount = 0;
};

// the plug-ins of one engine, by slot (raw pointers; ownership is elsewhere)
struct PluginRack
{
    std::array<std::atomic<PluginNode*>, NPADS> inst;
    std::array<std::array<std::atomic<PluginNode*>, PAD_VFX>, NPADS> padFx;
    std::array<std::atomic<PluginNode*>, MASTER_VFX> master;
    std::array<std::array<std::atomic<PluginNode*>, SEND_VFX>, NSEND> send;
    PluginRack();
    std::atomic<PluginNode*>& at (const SlotId& s);
    void forEach (const std::function<void (PluginNode*)>& fn);   // every non-null node
};

// a set of nodes owned together (used for offline renders: clones of the live plug-ins)
struct PluginSet
{
    std::map<SlotId, std::unique_ptr<PluginNode>> nodes;
    int masterLatency() const;
};
using PluginSetPtr = std::shared_ptr<PluginSet>;
} // namespace cx
