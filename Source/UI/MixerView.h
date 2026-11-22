#pragma once

#include "AppContext.h"
#include "Widgets.h"

class MixerView;

// The Analogue switch: a console lamp button. Dark when off, glowing orange when on, and it
// turns red as the channel is driven hard enough to break up.
class LampButton : public juce::Button
{
public:
    LampButton();
    float heat = 0.0f;      // 0 warm .. 1 distorting
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// One channel strip: name, trim with phase and mono, the slot's devices, send A / B, pan and width,
// the Analogue lamp, fader with level and gain-reduction meters, mute / solo, and the patch jacks.
// Tracks, buses, the two returns and the master are all strips; clicking one selects its slot so the
// device panel below shows its patch, and clicking a device slot jumps to that device.
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (AppContext&, int slot);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    const int slot;
    Track& track;

    bool hasOutJack() const { return ! isMaster; }
    bool hasInJack() const  { return track.takesInput(); }
    juce::Point<float> outJack() const { return outJackPos; }     // in this strip's coordinates
    juce::Point<float> inJack() const  { return inJackPos; }

    std::function<void (int busIndex)> onRemoveBus;

private:
    struct Insert { int moduleId; juce::String name; bool compressor; };

    AppContext& ctx;
    const bool isMaster, isReturn, isBus;
    float shownLevel = 0.0f, shownReduction = 0.0f, shownHeat = 0.0f;
    int clipHold = 0;
    int numInsertSlots = 8;

    juce::Slider trim  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider pan   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider width { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::OwnedArray<juce::Slider> sends;
    SmallToggle phase { juce::String::fromUTF8 ("\xc3\x98"), Pal::Green }, mono { "MO", Pal::Green };
    SmallToggle mute { "M", Pal::Amber }, solo { "S", Pal::Gold };
    LampButton analogue;
    juce::Rectangle<int> nameArea, meterArea, reductionArea, insertArea, sendArea, destArea;
    juce::Point<float> outJackPos, inJackPos;

    std::vector<Insert> inserts() const;
    void showMenu();
    void rename();
    void drawMeters (juce::Graphics&, juce::Colour) const;
};

// Sits over the strips and the trough beneath them: draws the patch cables hanging from each
// output jack to the bus or master it feeds, and handles patching. Drag an output jack onto a bus or
// the master, or click the jack and then the destination. Double-click a jack to send it to the master.
class PatchLayer : public juce::Component
{
public:
    PatchLayer (AppContext&, MixerView&);

    void reset();
    juce::Rectangle<int> trough;

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    AppContext& ctx;
    MixerView& owner;
    int from = -1, target = -1;
    bool dragging = false, armed = false, moved = false;
    juce::Point<float> end;

    int outJackAt (juce::Point<float>) const;
    int targetAt (juce::Point<float>) const;
    juce::Point<float> jack (int slot, bool output) const;
    static juce::Path cable (juce::Point<float> from, juce::Point<float> to, float maxSag);
};

// The mixer: channel strips over a patch trough. Tracks, then buses, Return A, Return B and Master.
class MixerView : public juce::Component
{
public:
    explicit MixerView (AppContext&);

    void rebuild();     // after the project's tracks or buses were replaced
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

    ChannelStrip* strip (int slot) const { return slot >= 0 && slot < strips.size() ? strips[slot] : nullptr; }
    int numStrips() const { return strips.size(); }

private:
    AppContext& ctx;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ChannelStrip> strips;
    PatchLayer patchLayer;
    juce::TextButton addBus { "+ Bus" }, vintage { "Vintage" };
    int builtGeneration = -1;
};
