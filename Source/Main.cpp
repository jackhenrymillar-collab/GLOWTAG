#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "Theme.h"

class GlowtagApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override { mainWindow = std::make_unique<MainWindow> (getApplicationName()); }
    void shutdown() override                       { mainWindow = nullptr; }
    void systemRequestedQuit() override            { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, Theme::get().bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), false);
            setResizable (true, true);
            setResizeLimits (1200, 760, 6000, 4000);

            // Large by default, but never bigger than the screen
            auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            centreWithSize (juce::jmin (1680, area.getWidth() - 40), juce::jmin (1040, area.getHeight() - 40));
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (GlowtagApplication)
