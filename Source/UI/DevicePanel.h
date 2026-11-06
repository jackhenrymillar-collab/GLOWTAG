#pragma once

#include "AppContext.h"
#include "Widgets.h"

// One module on the patch canvas: a framed card with an icon, title, numeral, knobs and jacks.
// Drag the header to move it; right-click it to remove it.
class ModuleCard : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    ModuleCard (AppContext&, Patch&, Track&, int trackIndex, int moduleId, int numeral);

    int getModuleId() const { return moduleId; }

    // Jack positions in this card's coordinates
    juce::Point<float> jackPosition (int port) const;
    std::vector<int> jacks() const;

    std::function<void()> onMoved, onRemove, onPatchChanged;   // onPatchChanged: sample or pad changed, rebuild
    float pulse = 0.0f;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    AppContext& ctx;
    Patch& patch;
    Track& track;
    int trackIndex;
    int moduleId;
    int numeral;
    juce::OwnedArray<juce::Slider> sliders;
    juce::Point<float> dragStart;
    juce::Rectangle<int> extraArea;     // waveform (Sampler), pads (Drum Rack), scope (LFO), meter (Compressor)
    bool dropHover = false;
    int litPad = -1;
    float shownDisplay = 0.0f;

    int padAt (juce::Point<int>) const;
    juce::Rectangle<float> padBounds (int pad) const;
    void showSampleMenu (std::function<void (const juce::String&)> choose);
    void playPad (int pad);

    const Module& module() const { return *patch.find (moduleId); }
    bool isEndpoint() const;
};

// Transparent layer above the cards that draws the cables and jacks and handles patching:
// drag from a jack to another to connect them, drag a plugged-in input to re-wire it,
// double-click or right-click a cable to remove it.
class CableLayer : public juce::Component
{
public:
    CableLayer (AppContext&, std::function<ModuleCard* (int)> cardForModule);

    void setPatch (Patch* p) { patch = p; repaint(); }
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
    Patch* patch = nullptr;
    std::function<ModuleCard* (int)> cardFor;

    JackRef dragFrom, hoverJack;
    bool dragging = false;
    juce::Point<float> dragEnd;
    int hoverCable = -1;

    juce::Point<float> jackPos (int module, int port) const;
    JackRef jackAt (juce::Point<float>, float radius = 9.0f) const;
    int cableAt (juce::Point<float>) const;
    juce::Path cablePath (const Cable&) const;
    static juce::Path cablePath (juce::Point<float> from, juce::Point<float> to, bool intoKnob);
    bool canConnect (JackRef a, JackRef b) const;
    juce::Colour colourFor (int module) const;
};

// Bottom panel: the selected track's modular device patch.
class DevicePanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit DevicePanel (AppContext&);
    ~DevicePanel() override;

    void addModule (ModuleType);
    void loadSample (const juce::String& nameOrPath);   // into the selected track's sampler, adding one if needed
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Canvas : public juce::Component
    {
        void paint (juce::Graphics&) override;
    };

    AppContext& ctx;
    juce::TextButton addButton { "+ Add module" }, tidyButton { "Tidy" };
    juce::Viewport viewport;
    Canvas canvas;
    juce::OwnedArray<ModuleCard> cards;
    std::unique_ptr<CableLayer> cables;

    void rebuild();
    void updateCanvasSize();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    ModuleCard* cardFor (int moduleId);
};
