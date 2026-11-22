#pragma once

#include "DeviceViews.h"

// The expanded view: a device's neon wall over the whole window. Double-click empty space on a
// device in the rack to open it; Esc closes it, [ and ] step through the track's devices.
class ExpandedDevice : public juce::Component, private juce::Timer
{
public:
    explicit ExpandedDevice (AppContext&);
    ~ExpandedDevice() override;

    void open (int track, int moduleId);
    void close();
    bool isOpen() const { return built != nullptr; }
    int openTrack() const { return device != nullptr ? device->track : -1; }

    std::function<void()> onClose;              // the rack refreshes its knobs
    std::function<void (int track)> onPatchChanged;   // a sample or pad changed: rebuild the track
    std::function<void()> onLookChanged;        // style or glow changed: worth saving

    // Remembered between sessions
    juce::var lookToVar() const;
    void lookFromVar (const juce::var&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;
    void rebuild();
    void step (int direction);                  // the previous or next device on the track
    void buildChrome();
    void loadAudio();
    juce::Point<float> toWall (juce::Point<float>) const;
    Neon::Wall* wallAt (juce::Point<float> screen, juce::Point<float>& where);

    AppContext& ctx;
    std::unique_ptr<DeviceViews::Device> device;
    std::unique_ptr<DeviceViews::Built> built;
    Neon::Wall chrome;                          // the strip along the top, in screen space
    juce::Colour chromeColours[3];
    juce::Rectangle<float> wallArea;
    float scale = 1.0f;
    int generation = 0;
    double lastFrame = 0.0;
    Neon::Wall* dragging = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<int, int> heldKeys;                // key code -> note
    juce::Image bricks;
};
