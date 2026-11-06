#pragma once

#include "AppContext.h"
#include "Widgets.h"

// One Eurorack-style module in the sequencer rack: a dark panel with screws, a spaced serif title,
// knobs and switches, a display and a row of jacks along the bottom. The Steps module carries
// sixteen pitch sliders with a gate button under each.
class SeqModuleView : public juce::Component
{
public:
    SeqModuleView (AppContext&, SeqRack&, int trackIndex, int moduleId);

    int getModuleId() const { return moduleId; }
    juce::Point<float> jackPosition (int port) const;   // in this component's coordinates
    int numPorts() const { return (int) info().ports.size(); }

    std::function<void()> onRemove, onMoveLeft, onMoveRight;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    AppContext& ctx;
    SeqRack& rack;
    int trackIndex, moduleId;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Slider> stepSliders;      // Steps only
    juce::Rectangle<int> displayArea, stepArea;
    std::array<float, 4> shown {};                   // smoothed copies of the live node's display values
    float clockLed = 0.0f, gateLed = 0.0f;
    int dragStep = -1;

    const SeqModule& module() const { return *rack.find (moduleId); }
    const SeqInfo& info() const { return seqInfo (module().type); }
    juce::Rectangle<float> stepButton (int step) const;
    void paintDisplay (juce::Graphics&, juce::Colour accent);
    void pushStep (int step);
};

// Cables between the modules. Drag jack to jack to patch; pull a plugged input to move its cable;
// double-click or right-click a cable to remove it.
class SeqCableLayer : public juce::Component
{
public:
    SeqCableLayer (AppContext&, std::function<SeqModuleView* (int)> viewForModule);

    void setRack (SeqRack* r) { rack = r; repaint(); }
    std::function<void()> onChanged;

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    struct JackRef { int module = -1, port = -1; bool valid() const { return module >= 0; } };

    AppContext& ctx;
    SeqRack* rack = nullptr;
    std::function<SeqModuleView* (int)> viewFor;
    JackRef dragFrom, hoverJack;
    bool dragging = false;
    juce::Point<float> dragEnd;
    int hoverCable = -1;

    bool isInput (int module, int port) const;
    juce::Point<float> jackPos (int module, int port) const;
    JackRef jackAt (juce::Point<float>, float radius = 9.0f) const;
    int cableAt (juce::Point<float>) const;
    juce::Path cablePath (const SeqCable&) const;
    static juce::Path cablePath (juce::Point<float> from, juce::Point<float> to);
    bool canConnect (JackRef a, JackRef b) const;
    juce::Colour colourFor (int module) const;
};

// The rack: a header strip and a scrolling rail of modules for the selected track.
class SequencerPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit SequencerPanel (AppContext&);
    ~SequencerPanel() override;

    static constexpr int railHeight = 300, sag = 36, headerHeight = 30;
    static constexpr int preferredHeight = headerHeight + railHeight + sag + 8;

    void addModule (SeqType);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Rail : public juce::Component { void paint (juce::Graphics&) override; };

    AppContext& ctx;
    juce::TextButton addButton { "+ Add module" }, clipsButton { "Back to clips" };
    juce::Viewport viewport;
    Rail rail;
    juce::OwnedArray<SeqModuleView> views;
    std::unique_ptr<SeqCableLayer> cables;
    int builtFor = -1;

    void rebuild();
    void layoutRail();
    void changed();   // rack edited: rebuild the runtime and the views
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    SeqModuleView* viewFor (int moduleId);
};
