// Hardware interface: the MPC Sample (hw_mpc.cpp) or the desktop simulator (hw_sim.cpp).
#pragma once
#include "common.h"

namespace cx
{
class Engine;
constexpr int SCREEN_W = 320, SCREEN_H = 240;

struct MidiMsg
{
    int src = 0;               // 0 = MPC control surface, 1.. = external MIDI (DIN, USB host, USB-C)
    uint8_t b[3] {};
    int n = 0;
};

class Hw
{
public:
    virtual ~Hw() = default;
    virtual bool init (std::string& error) = 0;
    virtual void shutdown() = 0;
    // a finished 320x240 0x00RRGGBB frame; only the dirty rect [x0,x1) x [y0,y1) changed
    virtual void present (const uint32_t* px, int x0, int y0, int x1, int y1) = 0;
    virtual bool displayBusy() = 0;
    virtual void sendSurface (const uint8_t* d, int n) = 0;
    // waits up to timeoutMs for MIDI, appends what arrived
    virtual void poll (int timeoutMs, std::vector<MidiMsg>& out) = 0;
    virtual bool startAudio (Engine& e, std::string& error) = 0;
    virtual void stopAudio() = 0;
    virtual void setInput (int route) = 0;         // 0 off, 1 line in, 2 built-in mic
    virtual void setMainVolume (float g) = 0;      // hardware MAIN VOLUME knob (0..1)
    virtual int latencyFrames() = 0;
    virtual std::string status() = 0;              // one line for the About page
    virtual bool simulated() const { return false; }
};

std::unique_ptr<Hw> makeMpcHw();
} // namespace cx
