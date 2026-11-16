// CONTRXL 25 - desktop edition (JUCE / C++)
// (c) 2026 Illahtainment. All rights reserved.
#include "ui/MainComponent.h"

#ifndef JUCE_APPLICATION_VERSION_STRING   // the Xcode (iPad) project doesn't pass it
 #define JUCE_APPLICATION_VERSION_STRING "0.84.0"
#endif

class ContrxlApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "CONTRXL 25"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&laf);
        window = std::make_unique<MainWindow> (*this);
        openFromCommandLine (commandLine);
    }

    // a song .zip or audio files given on the command line (or dropped on the .exe) are opened
    void anotherInstanceStarted (const juce::String& commandLine) override { openFromCommandLine (commandLine); }
    void openFromCommandLine (const juce::String& commandLine)
    {
        if (window == nullptr) return;
        juce::Array<juce::File> files;
        for (auto& a : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File f (a.unquoted());
            if (f.existsAsFile()) files.add (f);
        }
        if (! files.isEmpty()) window->content->openFiles (files);
    }

    // iPad: the app can be closed at any time once it is in the background, so save then
    void suspended() override { if (window != nullptr) window->content->saveForBackground(); }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr && ! window->content->confirmQuit())
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Unsaved recording",
                "You recorded something that is not saved yet. Quit anyway?", "Quit", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([] (int r) { if (r == 1) juce::JUCEApplication::getInstance()->quit(); }));
            return;
        }
        quit();
    }

private:
    // Scrolls the main layout when the window is smaller than the layout needs.
    struct Shell : public juce::Component
    {
        Shell()
        {
            view.setViewedComponent (&main, false);
            view.setScrollBarsShown (true, true);
            view.setScrollBarThickness (12);
            addAndMakeVisible (view);
            main.onLayoutChange = [this] { resized(); };
        }
        void resized() override
        {
            view.setBounds (getLocalBounds());
            const int sb = view.getScrollBarThickness();
            int w = getWidth(), h = getHeight();
            const int minW = cx::ui::MainComponent::minimumWidth, minH = main.minimumHeight();
            const bool needH = w < minW, needV = h < minH;
            if (needV) w -= sb;
            if (needH) h -= sb;
            main.setSize (juce::jmax (w, minW), juce::jmax (h, minH));
        }
        void paint (juce::Graphics& g) override { g.fillAll (cx::ui::C::bg); }
        cx::ui::MainComponent main;
        juce::Viewport view;
    };

    struct MainWindow : public juce::DocumentWindow
    {
        explicit MainWindow (ContrxlApp& a)
            : DocumentWindow ("CONTRXL 25", juce::Colours::black, DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);   // on the iPad: no title bar at all, the app fills the screen
            shell = new Shell();
            content = &shell->main;
            content->applyUiScale();
            setContentOwned (shell, false);
           #if CX_MOBILE
            setFullScreen (true);
            setVisible (true);
            content->grabKeyboardFocus();
            return;
           #endif
            setResizable (true, true);
            setResizeLimits (640, 400, 8000, 6000);   // smaller than the layout is fine: it scrolls
            if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = d->userArea;
                setSize (juce::jmin (1560, area.getWidth() - 20), juce::jmin (1020, area.getHeight() - 20));
            }
            else setSize (1400, 900);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            cx::ui::makeTitleBarDark (*this);
            content->grabKeyboardFocus();
        }
        void closeButtonPressed() override { app.systemRequestedQuit(); }
        ContrxlApp& app;
        Shell* shell = nullptr;
        cx::ui::MainComponent* content = nullptr;
    };

    cx::ui::CxLookAndFeel laf;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (ContrxlApp)
