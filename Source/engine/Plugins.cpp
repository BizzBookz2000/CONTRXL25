#include "Plugins.h"

namespace cx
{
juce::String SlotId::label() const
{
    switch (kind)
    {
        case Inst:   return padLabel (a) + " instrument";
        case PadFx:  return padLabel (a) + " insert " + juce::String (b + 1);
        case Master: return "Master insert " + juce::String (b + 1);
        case Send:   return juce::String ("Return ") + (char) ('A' + a) + " insert " + juce::String (b + 1);
        default:     return {};
    }
}

std::vector<SlotId> SlotId::all()
{
    std::vector<SlotId> v;
    for (int p = 0; p < NPADS; ++p)
    {
        v.push_back ({ Inst, p, 0 });
        for (int k = 0; k < PAD_VFX; ++k) v.push_back ({ PadFx, p, k });
    }
    for (int k = 0; k < MASTER_VFX; ++k) v.push_back ({ Master, 0, k });
    for (int s = 0; s < NSEND; ++s) for (int k = 0; k < SEND_VFX; ++k) v.push_back ({ Send, s, k });
    return v;
}

// ------------------------------------------------------------------ node
PluginNode::PluginNode (std::unique_ptr<juce::AudioProcessor> p, int instanceId, bool isInstrument)
    : proc (std::move (p)), iid (instanceId), instrument (isInstrument)
{
    if (proc) name = proc->getName();
    midi.ensureSize (8192);
}

PluginNode::~PluginNode()
{
    if (proc && prepared) proc->releaseResources();
}

void PluginNode::prepare (double sampleRate, int maxBlock, bool nonRealtime)
{
    if (! proc) return;
    if (prepared) proc->releaseResources();
    proc->enableAllBuses();
    proc->setNonRealtime (nonRealtime);
    proc->setRateAndBufferSizeDetails (sampleRate, maxBlock);
    proc->prepareToPlay (sampleRate, maxBlock);
    numIn = proc->getTotalNumInputChannels();
    numOut = proc->getTotalNumOutputChannels();
    maxN = maxBlock;
    scratch.setSize (juce::jmax (2, numIn, numOut), maxBlock, false, true, false);
    midi.clear();
    held.fill (0); heldCount = 0;
    prepared = true;
}

juce::String PluginNode::getState() const
{
    if (! proc) return {};
    juce::MemoryBlock mb;
    proc->getStateInformation (mb);
    return mb.getSize() > 0 ? mb.toBase64Encoding() : juce::String();
}

void PluginNode::setState (const juce::String& base64)
{
    if (! proc || base64.isEmpty()) return;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (base64) && mb.getSize() > 0)
        proc->setStateInformation (mb.getData(), (int) mb.getSize());
}

void PluginNode::addMidi (const juce::MidiMessage& m, int offset)
{
    midi.addEvent (m, juce::jlimit (0, juce::jmax (0, maxN - 1), offset));
}

void PluginNode::addNote (int note, int vel, int offset)
{
    note = juce::jlimit (0, 127, note);
    if (vel > 0)
    {
        addMidi (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, vel)), offset);
        if (held[(size_t) note] < 255) { ++held[(size_t) note]; ++heldCount; }
    }
    else
    {
        if (held[(size_t) note] == 0) return;
        addMidi (juce::MidiMessage::noteOff (1, note), offset);
        --held[(size_t) note]; --heldCount;
    }
}

void PluginNode::allNotesOff (int offset)
{
    for (int k = 0; k < 128; ++k)
        while (held[(size_t) k] > 0) { addMidi (juce::MidiMessage::noteOff (1, k), offset); --held[(size_t) k]; }
    heldCount = 0;
    addMidi (juce::MidiMessage::controllerEvent (1, 64, 0), offset);
    addMidi (juce::MidiMessage::allNotesOff (1), offset);
}

void PluginNode::process (float* L, float* R, int n) noexcept
{
    if (! prepared || ! proc || n <= 0 || n > maxN) { midi.clear(); return; }
    if (proc->isSuspended() || (bypass.load (std::memory_order_relaxed) && ! instrument)) { midi.clear(); return; }
    const int chans = scratch.getNumChannels();
    for (int c = 0; c < chans; ++c) juce::FloatVectorOperations::clear (scratch.getWritePointer (c), n);
    if (! instrument)
    {
        if (numIn >= 2) { scratch.copyFrom (0, 0, L, n); scratch.copyFrom (1, 0, R, n); }
        else if (numIn == 1) { scratch.copyFrom (0, 0, L, n, 0.5f); scratch.addFrom (0, 0, R, n, 0.5f); }
    }
    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), chans, n);
    {
        const juce::ScopedLock sl (proc->getCallbackLock());
        proc->processBlock (view, midi);
    }
    midi.clear();
    if (bypass.load (std::memory_order_relaxed)) return;   // bypassed instrument: keeps running silently
    const float* oL = scratch.getReadPointer (0);
    const float* oR = scratch.getReadPointer (numOut > 1 ? 1 : 0);
    if (instrument)
    {
        juce::FloatVectorOperations::add (L, oL, n);
        juce::FloatVectorOperations::add (R, oR, n);
    }
    else if (numOut > 0)
    {
        juce::FloatVectorOperations::copy (L, oL, n);
        juce::FloatVectorOperations::copy (R, oR, n);
    }
}

// ------------------------------------------------------------------ rack
PluginRack::PluginRack()
{
    for (auto& a : inst) a.store (nullptr);
    for (auto& r : padFx) for (auto& a : r) a.store (nullptr);
    for (auto& a : master) a.store (nullptr);
    for (auto& r : send) for (auto& a : r) a.store (nullptr);
}

std::atomic<PluginNode*>& PluginRack::at (const SlotId& s)
{
    const int a = juce::jlimit (0, NPADS - 1, s.a);
    switch (s.kind)
    {
        case SlotId::PadFx:  return padFx[(size_t) a][(size_t) juce::jlimit (0, PAD_VFX - 1, s.b)];
        case SlotId::Master: return master[(size_t) juce::jlimit (0, MASTER_VFX - 1, s.b)];
        case SlotId::Send:   return send[(size_t) juce::jlimit (0, NSEND - 1, s.a)][(size_t) juce::jlimit (0, SEND_VFX - 1, s.b)];
        default:             return inst[(size_t) a];
    }
}

void PluginRack::forEach (const std::function<void (PluginNode*)>& fn)
{
    for (auto& s : SlotId::all())
        if (auto* n = at (s).load()) fn (n);
}

int PluginSet::masterLatency() const
{
    int l = 0;
    for (auto& kv : nodes)
        if (kv.first.kind == SlotId::Master && kv.second && ! kv.second->bypass.load()) l += kv.second->latency();
    return l;
}
} // namespace cx
