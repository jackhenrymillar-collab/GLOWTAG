#include "BrowserPanel.h"
#include "../Engine/Samples.h"
#include "../Engine/PluginHost.h"
#include "DeviceCards.h"
#include "../Engine/DrumKit.h"

namespace
{
    const std::vector<BrowserPanel::Item> devices = {
        { "Sampler",    "Chop, slice and play",   Icon::Sampler,    Pal::Rose },
        { "Synth",      "Wavetable voice",        Icon::Synth,      Pal::Violet },
        { "Drum Rack",  "16 pads, analogue voices", Icon::Drums,    Pal::Amber },
        { "Drum Rack Sequencer", "Steps for the drum rack", Icon::Drums, Pal::Gold },
        { "Filter",     "Ladder low / high pass", Icon::Filter,     Pal::Sky },
        { "Delay",      "Tempo-synced echoes",    Icon::Delay,      Pal::Blue },
        { "Reverb",     "Rooms to halls",         Icon::Reverb,     Pal::Ice },
        { "EQ",         "Four-band parametric",   Icon::EQ,         Pal::Gold },
        { "Compressor", "Glue and punch",         Icon::Compressor, Pal::Green },
        { "Drive",      "Tube, tape, fuzz, fold", Icon::Drive,      Pal::Amber },
        { "LFO",        "Modulate any knob",      Icon::LFO,        Pal::Violet },
        { "Gas Tube",   "Neon tubes that strike and buzz",   Icon::GasTube,     Pal::Amber },
        { "Crate Digger", "Digs slices out of a record",     Icon::Crate,       Pal::Rose },
        { "String Wall", "Strings to pluck, bow or feed back", Icon::Strings,   Pal::Ice },
        { "Ghost Tag",  "Echoes that fade and drift",        Icon::Ghost,       Pal::Violet },
        { "Crowd",      "One sound sung by a crowd",         Icon::Crowd,       Pal::Gold },
        { "Transformer", "Circuit bending, broken parts",    Icon::Transformer, Pal::Green },
    };

    const ModuleType deviceTypes[] = { ModuleType::Sampler, ModuleType::Synth, ModuleType::Drums, ModuleType::DrumSeq, ModuleType::Filter,
                                       ModuleType::Delay, ModuleType::Reverb, ModuleType::EQ, ModuleType::Compressor,
                                       ModuleType::Drive, ModuleType::LFO,
                                       ModuleType::GasTube, ModuleType::CrateDigger, ModuleType::StringWall,
                                       ModuleType::GhostTag, ModuleType::Crowd, ModuleType::Transformer };

    Pal palForCategory (const juce::String& c)
    {
        if (c == "Drums") return Pal::Amber;
        if (c == "Vocals") return Pal::Violet;
        if (c == "FX") return Pal::Sky;
        return Pal::Ice;
    }

    const std::vector<BrowserPanel::Item> sequencers = {
        { "Rolling 16", "16 steps, five directions",     Icon::Steps,     Pal::Sky },
        { "Circadian",  "Day-and-night modulation",      Icon::Circadian, Pal::Amber },
        { "Euclid",     "Hits spread round the circle",  Icon::Euclid,    Pal::Gold },
        { "Chance",     "Probability gate and drift",    Icon::Chance,    Pal::Rose },
        { "Tide LFO",   "Synced, a 16th to 8 bars",      Icon::Tide,      Pal::Sky },
        { "Quantise",   "Onto any scale",                Icon::Quantise,  Pal::Ice },
        { "Turing",     "Shift-register melodies",       Icon::Turing,    Pal::Violet },
        { "Mutate",     "Slow pitch wander",             Icon::Mutate,    Pal::Violet },
        { "Burst",      "Ratchets from one trigger",     Icon::Burst,     Pal::Amber },
    };
    const SeqType sequencerTypes[] = { SeqType::Steps, SeqType::Circadian, SeqType::Euclid, SeqType::Chance, SeqType::Tide,
                                       SeqType::Quantise, SeqType::Turing, SeqType::Mutate, SeqType::Burst };
}

BrowserPanel::BrowserPanel (AppContext& c) : ctx (c)
{
    const char* names[] = { "Samples", "Devices", "Seq", "Plugins" };
    for (int i = 0; i < numTabs; ++i)
    {
        auto& b = tabs[i];
        b.setButtonText (names[i]);
        b.setRadioGroupId (2001);
        b.setClickingTogglesState (true);
        b.getProperties().set ("pal", (int) Pal::Green);
        b.onClick = [this, i] { if (tabs[i].getToggleState()) { tab = i; chosen = hovered = -1; scanButton.setVisible (tab == 3); resized(); repaint(); } };
        addAndMakeVisible (b);
    }
    tabs[tab].setToggleState (true, juce::dontSendNotification);

    scanButton.getProperties().set ("pal", (int) Pal::Sky);
    scanButton.setTooltip ("Look through the standard AU and VST3 folders");
    scanButton.onClick = [this]
    {
        if (PluginHost::get().isScanning()) return;
        PluginHost::get().scan ([safe = juce::Component::SafePointer<BrowserPanel> (this)]
        {
            if (safe != nullptr) { safe->pluginItems.clear(); safe->scanButton.setButtonText ("Scan for plugins"); safe->repaint(); }
        });
        startTimerHz (10);
    };
    addChildComponent (scanButton);
}

void BrowserPanel::timerCallback()
{
    if (PluginHost::get().isScanning())
    {
        scanButton.setButtonText ("Scanning " + juce::String (juce::roundToInt (PluginHost::get().scanProgress() * 100.0f)) + "%");
        repaint();
    }
    else
    {
        stopTimer();
        scanButton.setButtonText ("Scan for plugins");
        pluginItems.clear();
        repaint();
    }
}

void BrowserPanel::refreshPlugins() const
{
    pluginItems.clear();
    pluginList = PluginHost::get().list();
    for (auto& d : pluginList)
        pluginItems.push_back ({ d.name, d.pluginFormatName + " / " + (d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("unknown")),
                                 Icon::Plugin, d.isInstrument ? Pal::Violet : Pal::Sky });
}

const std::vector<BrowserPanel::Item>& BrowserPanel::items() const
{
    if (tab == 0)
    {
        if (sampleItems.empty())
            for (auto& e : Samples::bank())
                sampleItems.push_back ({ e.name, e.category + " / " + juce::String (e.data->lengthSeconds(), 2) + " s",
                                         e.category == "Drums" ? Icon::Drums : Icon::Sampler, palForCategory (e.category) });
        return sampleItems;
    }
    if (tab == 3)
    {
        if (pluginItems.empty() || pluginItems.size() != (size_t) PluginHost::get().known.getNumTypes())
            refreshPlugins();
        return pluginItems;
    }
    return tab == 1 ? devices : sequencers;
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (28); // heading
    auto tabRow = r.removeFromTop (28);
    const int w = tabRow.getWidth() / numTabs;
    for (auto& b : tabs)
        b.setBounds (tabRow.removeFromLeft (w).reduced (2, 0));
    r.removeFromTop (12);
    scanButton.setBounds (r.removeFromTop (28));
    if (tab == 3) r.removeFromTop (10);
    else r.translate (0, -38);   // other tabs keep the list at the top
    listArea = r;
}

juce::Rectangle<int> BrowserPanel::itemBounds (int index) const
{
    const int h = tab == 0 || tab == 3 ? 34 : 54;
    return { listArea.getX(), listArea.getY() + index * (h + (tab == 0 || tab == 3 ? 4 : 6)), listArea.getWidth(), h };
}

int BrowserPanel::itemAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) items().size(); ++i)
        if (itemBounds (i).contains (p))
            return i;
    return -1;
}

void BrowserPanel::mouseMove (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());
    if (i != hovered)
    {
        hovered = i;
        repaint();
        // A sample plays as the pointer comes onto it
        if (tab == 0 && i >= 0 && Neon::now() - hoveredAt > 0.12) previewSample (i);
        hoveredAt = Neon::now();
    }
}

void BrowserPanel::previewSample (int index)
{
    const auto& bank = Samples::bank();
    if (index >= 0 && index < (int) bank.size())
        ctx.engine.preview (bank[(size_t) index].data);
}

void BrowserPanel::mouseDrag (const juce::MouseEvent& e)
{
    // Drag a sample onto a Drum Rack pad, a Sampler or the Crate Digger
    if (tab != 0 || chosen < 0 || e.getDistanceFromDragStart() < 6) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
    {
        const auto& it = items()[(size_t) chosen];
        juce::Image img (juce::Image::ARGB, 160, 28, true);
        {
            juce::Graphics ig (img);
            ig.setColour (juce::Colour (0xcc1b1c1f));
            ig.fillRoundedRectangle (0, 0, 160, 28, 8);
            ig.setColour (Theme::get().themeColour());
            ig.setFont (Theme::get().body (13.0f));
            ig.drawText (it.name, 10, 0, 140, 28, juce::Justification::centredLeft);
        }
        container->startDragging ("sample:" + it.name, this, juce::ScaledImage (img), true);
    }
}

void BrowserPanel::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void BrowserPanel::mouseDown (const juce::MouseEvent& e)
{
    chosen = itemAt (e.getPosition());
    if (tab == 0) previewSample (chosen);
    repaint();
}

void BrowserPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());
    if (tab == 1 && i >= 0 && i < (int) std::size (deviceTypes) && onAddDevice)
        onAddDevice (deviceTypes[i]);
    else if (tab == 0 && i >= 0 && onLoadSample)
        onLoadSample (items()[(size_t) i].name);
    else if (tab == 2 && i >= 0 && i < (int) std::size (sequencerTypes) && onAddSequencer)
        onAddSequencer (sequencerTypes[i]);
    else if (tab == 3 && i >= 0 && i < pluginList.size() && onAddPlugin)
        onAddPlugin (pluginList.getReference (i));
}

void BrowserPanel::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    const auto& list = items();
    const int bottom = tab == 3 ? listArea.getBottom() : getHeight() - 8;
    const auto grey = juce::Colour (0xff6d7077);
    const auto theme = th.themeColour();
    const double t = Neon::now();

    Neon::paintLit (g, getWidth(), getHeight(), glowLayer, 0.8f, [&] (juce::Graphics& gg, bool real)
    {
        auto head = getLocalBounds().reduced (12).removeFromTop (24).toFloat();
        Neon::text (gg, "browser", head.getX() + 2.0f, head.getBottom() - 3.0f, 24.0f, theme, 0.9f);

        if (tab == 3 && list.empty())
        {
            if (real)
            {
                gg.setColour (th.muted);
                gg.setFont (th.body (12.0f));
                const bool scanning = PluginHost::get().isScanning();
                gg.drawFittedText (scanning ? "Scanning\n" + PluginHost::get().scanningName()
                                            : "No plugins listed yet.\n\nScan to find the AU and VST3 plugins on this Mac, then double-click one to add it to the selected track.",
                                   listArea.reduced (8), juce::Justification::centredTop, 8);
            }
            return;
        }

        for (int i = 0; i < (int) list.size(); ++i)
        {
            auto r = itemBounds (i).toFloat();
            if (r.getBottom() > (float) bottom)
                break;

            const auto& it = list[(size_t) i];
            const bool lit = i == hovered || i == chosen;
            auto c = th.isRainbow() ? th.pal (it.pal) : theme;
            // Drum samples take the pad colour of the range they play
            if (tab == 0 && Neon::look().padColours && it.detail.startsWith ("Drums"))
            {
                const auto range = DrumKit::rangeOfName (it.name);
                c = range == DrumKit::Range::Low ? juce::Colour (0xffff6a2a) : range == DrumKit::Range::High ? juce::Colour (0xff38a8ff) : juce::Colour (0xffff3fa4);
            }

            // A faint pane under the pointer or the chosen row, nothing more
            if (real && lit)
            {
                gg.setColour (juce::Colours::white.withAlpha (i == chosen ? 0.05f : 0.03f));
                gg.fillRoundedRectangle (r, 8.0f);
            }

            if (tab == 1)
            {
                // A device: its small neon piece, its name in lit lettering, a grey line under it
                const auto type = deviceTypes[i];
                juce::Colour cols[3];
                if (th.isRainbow()) DeviceCards::palette (type, cols);
                else { cols[0] = theme; cols[1] = Neon::mix (theme, juce::Colours::white, 0.45f); cols[2] = Neon::mix (theme, juce::Colours::white, 0.22f); }
                if (! lit) for (auto& col : cols) col = col.withMultipliedAlpha (0.75f);
                DeviceCards::artScaled (gg, type, r.getX() + 8.0f, r.getY() + 10.0f, 0.62f, cols, t);
                Neon::text (gg, it.name.toLowerCase(), r.getX() + 58.0f, r.getY() + 26.0f, 19.0f, cols[1], lit ? 1.0f : 0.75f);
                Neon::label (gg, it.detail.toLowerCase(), r.getX() + 59.0f, r.getY() + 43.0f, Neon::Align::left, grey, 12.0f);
            }
            else if (tab == 2)
            {
                // A sequencer module: a lamp in its colour and its name
                Neon::bulb (gg, r.getX() + 18.0f, r.getCentreY() - 6.0f, c, lit ? 1.0f : 0.55f, 4.5f);
                Neon::text (gg, it.name.toLowerCase(), r.getX() + 36.0f, r.getY() + 26.0f, 19.0f, Neon::mix (c, juce::Colours::white, 0.3f), lit ? 1.0f : 0.7f);
                Neon::label (gg, it.detail.toLowerCase(), r.getX() + 37.0f, r.getY() + 43.0f, Neon::Align::left, grey, 12.0f);
            }
            else
            {
                // A sample or a plugin: a small lamp, the name in neon, its detail in grey on the right
                Neon::bulb (gg, r.getX() + 12.0f, r.getCentreY(), c, lit ? 1.0f : 0.45f, 3.2f);
                const float detailW = juce::jmin (r.getWidth() * 0.45f, Neon::textWidth (it.detail.toLowerCase(), 11.0f) * 0.62f + 8.0f);
                const float room = r.getWidth() - 34.0f - detailW;
                const float size = juce::jlimit (11.0f, 16.0f, 16.0f * room / juce::jmax (1.0f, Neon::textWidth (it.name.toLowerCase(), 16.0f)));
                Neon::text (gg, it.name.toLowerCase(), r.getX() + 26.0f, r.getCentreY() + size * 0.32f, size, Neon::mix (c, juce::Colours::white, 0.35f), lit ? 1.0f : 0.6f);
                Neon::label (gg, it.detail.toLowerCase(), r.getRight() - 8.0f, r.getCentreY() + 4.0f, Neon::Align::right, grey, 11.0f);
            }
        }
    });
}
