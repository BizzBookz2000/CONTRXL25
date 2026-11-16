#include "MidiLearn.h"
#include "ui/KeyboardView.h"

namespace cx::ui
{
static juce::Component* panelOf (juce::Component* c)
{
    for (auto* p = c; p != nullptr; p = p->getParentComponent())
        if (p->getComponentID().isNotEmpty()) return p;
    return nullptr;
}

static LSlider* lsliderOf (juce::Component* c)
{
    for (auto* p = c; p != nullptr; p = p->getParentComponent())
        if (auto* s = dynamic_cast<LSlider*> (p)) return s;
    return nullptr;
}

static juce::String baseText (juce::Component* c)
{
    if (auto* b = dynamic_cast<Btn*> (c)) return "btn:" + b->getProperties()["learnText"].toString();
    if (auto* s = dynamic_cast<LSlider*> (c)) return "slider:" + s->label.getText();
    if (auto* s = dynamic_cast<juce::Slider*> (c)) if (s->getProperties().contains ("learnName")) return "slider:" + s->getProperties()["learnName"].toString();
    return {};
}

static void dfs (juce::Component* c, const std::function<void (juce::Component*)>& fn)
{
    for (auto* ch : c->getChildren()) { fn (ch); dfs (ch, fn); }
}

juce::String learnId (juce::Component* c)
{
    if (c == nullptr) return {};
    if (c->getComponentID() == "master") return "top/master";
    const auto base = baseText (c);
    if (base.isEmpty()) return {};
    auto* panel = panelOf (c->getParentComponent());
    if (panel == nullptr) return {};
    int index = 0; bool found = false;
    dfs (panel, [&] (juce::Component* x) { if (found) return; if (x == c) { found = true; return; } if (baseText (x) == base) ++index; });
    return panel->getComponentID() + "/" + base + "#" + juce::String (index);
}

juce::Component* findLearnable (juce::Component* root, const juce::String& id)
{
    juce::Component* hit = nullptr;
    dfs (root, [&] (juce::Component* x)
    {
        if (hit != nullptr) return;
        if ((dynamic_cast<Btn*> (x) != nullptr || dynamic_cast<LSlider*> (x) != nullptr || x->getComponentID() == "master"
             || (dynamic_cast<juce::Slider*> (x) != nullptr && x->getProperties().contains ("learnName"))) && learnId (x) == id) hit = x;
    });
    return hit;
}

void collectLearnables (juce::Component* root, juce::Array<juce::Component*>& out)
{
    dfs (root, [&] (juce::Component* x)
    {
        if (! x->isShowing()) return;
        if (dynamic_cast<PadButton*> (x) || dynamic_cast<PitchKnob*> (x) || dynamic_cast<KeyboardView*> (x) || dynamic_cast<LSlider*> (x)
            || (dynamic_cast<juce::Slider*> (x) && x->getProperties().contains ("learnName"))
            || x->getComponentID() == "master"
            || (dynamic_cast<Btn*> (x) && ! (bool) x->getProperties()["noLearn"] && panelOf (x) != nullptr))
            out.add (x);
    });
}

bool learnTargetFor (juce::Component* c, juce::Point<int> screenPos, MidiTarget& t)
{
    for (auto* p = c; p != nullptr; p = p->getParentComponent())
    {
        if (auto* pad = dynamic_cast<PadButton*> (p)) { t = { "pad", pad->slot(), {}, "Pad " + juce::String (pad->slot() + 1) }; return true; }
        if (auto* kb = dynamic_cast<KeyboardView*> (p))
        {
            const int k = kb->keyAtPoint (kb->getLocalPoint (nullptr, screenPos).toFloat());
            if (k < 0) return false;
            t = { "key", k, {}, "Key " + juce::String (k + 1) };
            return true;
        }
        if (auto* knob = dynamic_cast<PitchKnob*> (p)) { const int n = (int) knob->getProperties()["knob"]; t = { "knob", n, {}, "Pitch knob " + juce::String (n + 1) }; return true; }
        if (p->getComponentID() == "master") { t = { "slider", 0, "top/master", "Master volume" }; return true; }
        if (auto* sl = dynamic_cast<juce::Slider*> (p))
            if (sl->getProperties().contains ("learnName"))
            {
                const auto id = learnId (sl);
                if (id.isEmpty()) return false;
                t = { "slider", 0, id, "Mixer " + sl->getProperties()["learnName"].toString() };
                return true;
            }
        if (auto* ls = lsliderOf (p))
        {
            const auto id = learnId (ls);
            if (id.isEmpty()) return false;
            auto* panel = panelOf (ls->getParentComponent());
            t = { "slider", 0, id, (panel ? panel->getName() + " " : juce::String()) + ls->label.getText().toLowerCase() };
            return true;
        }
        if (auto* b = dynamic_cast<Btn*> (p))
        {
            if ((bool) b->getProperties()["noLearn"]) return false;
            if (b->getProperties().contains ("bank")) { const int n = (int) b->getProperties()["bank"]; t = { "bank", n, {}, juce::String ("Bank ") + (char) ('A' + n) }; return true; }
            const auto id = learnId (b);
            if (id.isEmpty()) return false;
            auto* panel = panelOf (b->getParentComponent());
            t = { "btn", 0, id, (panel ? panel->getName() + " " : juce::String()) + b->getProperties()["learnText"].toString() };
            return true;
        }
    }
    return false;
}

void LearnOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.12f));
    juce::Array<juce::Component*> list;
    collectLearnables (&root, list);
    const juce::Colour linkCol (0xff35d07f);
    for (auto* c : list)
    {
        if (auto* kb = dynamic_cast<KeyboardView*> (c))
        {
            // each key separately
            for (int k = 0; k < ctl.P.s.keys; ++k)
            {
                MidiTarget t { "key", k, {}, {} };
                const auto sig = ctl.midiSigFor (t);
                const bool armed = ctl.learnTarget && *ctl.learnTarget == t;
                if (sig.isEmpty() && ! armed) continue;
                auto r = getLocalArea (kb, kb->keyBounds (k).toNearestInt()).toFloat().reduced (1.0f);
                g.setColour (armed ? (blink ? linkCol : juce::Colours::transparentBlack) : linkCol);
                g.drawRect (r, 2.0f);
                if (! sig.isEmpty()) { g.setFont (mono (9.0f, true)); g.drawText (Controller::sigName (sig), r.removeFromBottom (24), juce::Justification::centred); }
            }
            continue;
        }
        MidiTarget t;
        if (! learnTargetFor (c, c->getScreenBounds().getCentre(), t)) continue;
        const auto sig = ctl.midiSigFor (t);
        const bool armed = ctl.learnTarget && *ctl.learnTarget == t;
        auto r = getLocalArea (c, c->getLocalBounds()).toFloat().reduced (0.5f);
        if (armed) { g.setColour (blink ? linkCol : linkCol.withAlpha (0.25f)); g.drawRoundedRectangle (r, 5.0f, 3.0f); }
        else if (sig.isNotEmpty()) { g.setColour (linkCol); g.drawRoundedRectangle (r, 5.0f, 2.0f); }
        else { g.setColour (C::accent.withAlpha (0.7f)); g.drawRoundedRectangle (r, 5.0f, 1.3f); }
        if (sig.isNotEmpty())
        {
            const auto txt = Controller::sigName (sig);
            g.setFont (mono (9.5f, true));
            const float bw = juce::jmin (r.getWidth(), 10.0f + 6.5f * (float) txt.length());
            auto badge = juce::Rectangle<float> (r.getRight() - bw + 2, r.getY() - 2, bw, 13);
            g.setColour (linkCol); g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colours::black); g.drawText (txt, badge, juce::Justification::centred);
        }
    }
    g.setColour (linkCol); g.setFont (mono (12.0f, true));
    g.drawText ("MIDI LEARN - click a control, then move / press it on your controller.  Esc or MIDI Learn = done.",
                getLocalBounds().removeFromBottom (22).reduced (8, 2), juce::Justification::centredRight);
}

void LearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    setInterceptsMouseClicks (false, false);
    auto* under = root.getComponentAt (root.getLocalPoint (this, e.getPosition()));
    setInterceptsMouseClicks (true, true);
    if (under == nullptr) return;
    // the MIDI Learn button itself (and the MIDI settings button) still work as normal buttons
    for (auto* p = under; p != nullptr; p = p->getParentComponent())
        if (auto* b = dynamic_cast<Btn*> (p))
            if ((bool) b->getProperties()["noLearn"]) { if (b->onClick) b->onClick(); return; }
    MidiTarget t;
    if (learnTargetFor (under, e.getScreenPosition(), t)) ctl.armLearn (t);
    else ctl.status ("THAT CONTROL CAN'T BE LINKED - CLICK A PAD, KEY, KNOB, BUTTON OR SLIDER");
    repaint();
}
} // namespace cx::ui
