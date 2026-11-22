#pragma once

#include "AppContext.h"
#include "Widgets.h"
#include "DeviceCards.h"
#include <juce_gui_extra/juce_gui_extra.h>

// A hosted plugin's own window. It keeps the graph that owns the plugin alive until it closes.
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& title, std::shared_ptr<Graph> graph, juce::AudioProcessor& processor, int slot, int moduleId);
    ~PluginWindow() override;

    const int slot, moduleId;
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::shared_ptr<Graph> graph;
};

// One module on the patch canvas: a neon card with its small art piece, its name in lit lettering,
// and sign words and tube faders (each fader's modulation jack sits at its left end). Drag empty
// space to move it; right-click it to remove it; double-click empty space for the expanded view.
class ModuleCard : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    ModuleCard (AppContext&, Patch&, Track&, int trackIndex, int moduleId, int numeral);

    int getModuleId() const { return moduleId; }

    // Jack positions in this card's coordinates
    juce::Point<float> jackPosition (int port) const;
    std::vector<int> jacks() const;

    std::function<void()> onMoved, onRemove, onPatchChanged;   // onPatchChanged: sample or pad changed, rebuild
    std::function<void()> onRaise;                               // clicked: come forward, but stay under the cables
    std::function<void()> onOpenPlugin;                          // Plugin: double-click opens its editor
    std::function<void()> onOpenExpanded;                        // built-in device: double-click opens its expanded view
    float pulse = 0.0f;
    float flash = 0.0f;      // lit up after the mixer jumped here

    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

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
    std::unique_ptr<DeviceCards::Card> card;
    Neon::GlowLayer glowLayer;
    juce::Point<float> dragStart;
    bool dropHover = false, onControl = false, moving = false;
    float shownDisplay = 0.0f;
    double lastPaint = 0.0, lastReadout = -10.0;

    void showSampleMenu (std::function<void (const juce::String&)> choose);
    void chooseSample();

    const Module& module() const { return *patch.find (moduleId); }
    bool isEndpoint() const;
    bool isUtility() const;
};

// Transparent layer above the cards that draws the cables and jacks and handles patching:
// drag from a jack to another to connect them, drag a plugged-in input to re-wire it,
// double-click or right-click a cable to remove it. A cable only lands on a jack that carries the
// same kind of signal: sound, notes or modulation.
class CableLayer : public juce::Component
{
public:
    CableLayer (AppContext&, std::function<ModuleCard* (int)> cardForModule);

    void setPatch (Patch* p) { patch = p; repaint(); }
    std::function<void()> onChanged;
    std::function<void (const juce::String&)> onHint;      // what the jack under the pointer is, or why a cable won't go there
    float rowOffset = 0.0f;                                // how far down the rows sit (they are centred in full screen)

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

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
    bool skipsCard (float fromX, float toX) const;      // a card lies between these two points on the device row
    juce::Path cablePath (juce::Point<float> from, juce::Point<float> to, bool intoKnob) const;
    bool canConnect (JackRef a, JackRef b) const;
    juce::String describe (JackRef) const;
    juce::String whyNot (JackRef from, JackRef to) const;
    void hint (const juce::String&);
    juce::String lastHint;
    juce::Colour colourFor (int module) const;
};

// Bottom panel: the selected track's modular device patch.
class DevicePanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit DevicePanel (AppContext&);
    ~DevicePanel() override;

    void addModule (ModuleType);
    void addPlugin (const juce::PluginDescription&);
    void openPluginEditor (int slot, int moduleId);
    void closePluginWindows();
    void loadSample (const juce::String& nameOrPath);   // into the selected track's sampler, adding one if needed
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Full screen: the patch fills the window, drawn larger. Double-click empty space to open it, Esc closes.
    bool isFullScreen() const { return fullScreen; }
    void setFullScreen (bool);
    std::function<void()> onFullScreenChanged;
    std::function<void (int track, int moduleId)> onOpenExpanded;
    void refresh() { rebuild(); }       // the patch changed elsewhere (the expanded view): show it again

    // Height the panel wants in the window: the device row, the utility row and the header
    static constexpr int preferredHeight = 36 + (int) Patch::rowsBottom + 22 + 8 + 6;

private:
    struct Canvas : public juce::Component
    {
        std::function<void()> onDoubleClick;
        void paint (juce::Graphics&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    };

    AppContext& ctx;
    juce::TextButton addButton { "+ add" }, tidyButton { "tidy" }, fullButton { "full screen" };
    juce::Viewport viewport;
    juce::Component holder;               // what the viewport scrolls: the canvas, scaled up in full screen
    Canvas canvas;
    bool fullScreen = false;
    float zoom = 1.0f, rowOffset = 0.0f;
    juce::String hintText;
    juce::OwnedArray<ModuleCard> cards;
    std::unique_ptr<CableLayer> cables;
    juce::OwnedArray<PluginWindow> pluginWindows;
    Neon::GlowLayer headGlow;

    void rebuild();
    void keepTrackSound();
    void updateCanvasSize();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    ModuleCard* cardFor (int moduleId);
};
