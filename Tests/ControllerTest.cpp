// Headless test of the controller's live-recording rules (which track a recorded hit goes into).
#include "app/Controller.h"
#include <iostream>

using namespace cx;
static int fails = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++fails;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Engine engine;
    Controller ctl (engine);
    auto& P = ctl.P;
    for (auto& t : P.tracks) { t.pd.fill (std::nullopt); t.arm = false; }
    auto hit = [&] (int pad, int pat, double pos = 0.0)
    {
        EngineEvent e; e.type = EngineEvent::RecHit; e.a = pad; e.b = -1; e.c = pat; e.f = 0; e.g = 0.8f; e.pos = pos;
        ctl.applyRecHit (e);
    };
    auto stepsIn = [&] (int track, int pat) { auto* d = P.tracks[(size_t) track].find (pat); return d ? (int) d->ev.size() : 0; };

    // pattern 1: track 1 holds A1
    P.tracks[0].arm = true; ctl.ui.lane = 0;
    hit (0, 0);
    check (stepsIn (0, 0) == 1 && P.tracks[0].pad == 0, "pattern 1: track 1 records A1");
    // pattern 2: the same track takes A5 for pattern 2 only
    P.s.pat = 1;
    hit (4, 1);
    check (stepsIn (0, 1) == 1 && P.tracks[0].padIn (1) == 4 && P.tracks[0].padIn (0) == 0, "pattern 2: armed track 1 records A5 (its pattern-1 pad stays A1)");
    // several armed tracks: each new pad takes the next armed, empty track
    for (auto& t : P.tracks) t.arm = false;
    P.tracks[4].arm = true; P.tracks[5].arm = true; ctl.ui.lane = 9;
    hit (10, 1); hit (11, 1, 2.0); hit (10, 1, 4.0);
    check (stepsIn (4, 1) == 2 && P.tracks[4].padIn (1) == 10 && stepsIn (5, 1) == 1 && P.tracks[5].padIn (1) == 11,
           "two armed tracks: A11 -> track 5, A12 -> track 6, A11 again -> track 5");
    // the selected armed track wins when it can take the pad
    P.tracks[7].arm = true; ctl.ui.lane = 7;
    hit (20, 1);
    check (stepsIn (7, 1) == 1 && P.tracks[7].padIn (1) == 20, "the selected armed track gets the hit");
    // locked tracks are never written
    for (auto& t : P.tracks) t.arm = false;
    P.tracks[8].arm = true; P.tracks[8].lock = true; P.tracks[9].arm = true; ctl.ui.lane = 8;
    hit (30, 1);
    check (stepsIn (8, 1) == 0 && stepsIn (9, 1) == 1, "a locked track is skipped, the next armed track records");
    P.tracks[9].lock = true;
    hit (31, 1, 3.0);
    check (stepsIn (8, 1) == 0 && stepsIn (9, 1) == 1, "all armed tracks locked: nothing is recorded");
    // an armed track that already holds another pad in this pattern is not overwritten
    P.tracks[9].lock = false; for (auto& t : P.tracks) t.arm = false; P.tracks[9].arm = true; ctl.ui.lane = 9;
    hit (40, 1, 5.0);
    check (P.tracks[9].padIn (1) == 30 && stepsIn (9, 1) == 1, "a track holding another pad in this pattern keeps it");
    // nothing armed
    for (auto& t : P.tracks) t.arm = false;
    hit (2, 1);
    check (P.tracks[2].find (1) == nullptr, "nothing armed: nothing recorded");
    // mixer helpers
    ctl.armPadTrack (11);
    check (P.tracks[5].arm, "mixer R arms the track that plays that pad");
    ctl.armPadTrack (50);
    const int l50 = ctl.trackForPad (50);
    check (l50 >= 0 && P.tracks[(size_t) l50].arm, "mixer R on a pad without a track picks an empty one");
    // chop-mode keys stay on one track (each note keeps its own chop)
    for (auto& t : P.tracks) { t.pd.fill (std::nullopt); t.arm = false; t.lock = false; }
    P.s.pat = 2; P.tracks[0].arm = true; P.tracks[1].arm = true; ctl.ui.lane = 0;
    auto hitChop = [&] (int pad, int chop, double pos)
    {
        EngineEvent e; e.type = EngineEvent::RecHit; e.a = pad; e.b = chop; e.c = 2; e.f = 0; e.g = 0.8f; e.pos = pos;
        ctl.applyRecHit (e);
    };
    hitChop (6, 0, 0.0); hitChop (6, 3, 1.0); hitChop (6, 5, 2.0);
    {
        auto* d = P.tracks[0].find (2);
        check (d && d->ev.size() == 3 && P.tracks[1].find (2) == nullptr, "keys in chop mode: all notes on ONE track");
        bool chopsKept = d && d->ev.size() == 3;
        if (chopsKept) for (auto& x : d->ev) chopsKept &= (x.c == -2 || x.c == 3 || x.c == 5);
        check (chopsKept, "each note keeps the chop that was played");
    }
    // a pad's own armed track wins over the selected empty track
    P.tracks[3].arm = true; ctl.ui.lane = 1;   // track 2 selected + empty, track 4 plays A4
    hit (3, 2, 3.0);
    check (stepsIn (3, 2) == 1 && P.tracks[1].find (2) == nullptr, "A4 goes to armed track 4 (its own), not the selected empty track");
    // layer mode: every armed track records
    ctl.ui.recAllArmed = true;
    hit (3, 2, 6.0);
    check (stepsIn (3, 2) == 2 && stepsIn (0, 2) == 4 && stepsIn (1, 2) == 1, "Rec all armed: every armed track records the hit");
    ctl.ui.recAllArmed = false;

    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << std::endl;
    return fails == 0 ? 0 : 1;
}
