#include "BrowserPanel.h"
#include "../Engine/Samples.h"
#include "../Engine/PluginHost.h"

namespace
{
    const std::vector<BrowserPanel::Item> devices = {
        { "Sampler",    "Chop, slice and play",   Icon::Sampler,    Pal::Rose },
        { "Synth",      "Wavetable voice",        Icon::Synth,      Pal::Violet },
        { "Drum Rack",  "16 pads, step grid",     Icon::Drums,      Pal::Amber },
        { "Filter",     "Ladder low / high pass", Icon::Filter,     Pal::Sky },
        { "Delay",      "Tempo-synced echoes",    Icon::Delay,      Pal::Blue },
        { "Reverb",     "Rooms to halls",         Icon::Reverb,     Pal::Ice },
        { "EQ",         "Four-band parametric",   Icon::EQ,         Pal::Gold },
        { "Compressor", "Glue and punch",         Icon::Compressor, Pal::Green },
        { "Drive",      "Tube, tape, fuzz, fold", Icon::Drive,      Pal::Amber },
        { "LFO",        "Modulate any knob",      Icon::LFO,        Pal::Violet },
    };

    const ModuleType deviceTypes[] = { ModuleType::Sampler, ModuleType::Synth, ModuleType::Drums, ModuleType::Filter,
                                       ModuleType::Delay, ModuleType::Reverb, ModuleType::EQ, ModuleType::Compressor,
                                       ModuleType::Drive, ModuleType::LFO };

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
    if (i != hovered) { hovered = i; repaint(); }
}

void BrowserPanel::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void BrowserPanel::mouseDown (const juce::MouseEvent& e)
{
    chosen = itemAt (e.getPosition());
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

    auto head = getLocalBounds().reduced (12).removeFromTop (24).toFloat();
    g.setColour (th.text);
    Glow::drawSpacedText (g, "BROWSER", head, th.heading (14.0f), 2.5f, juce::Justification::centredLeft);

    const auto& list = items();
    const int bottom = tab == 3 ? listArea.getBottom() : getHeight() - 8;
    if (tab == 3 && list.empty())
    {
        g.setColour (th.muted);
        g.setFont (th.body (12.0f));
        const bool scanning = PluginHost::get().isScanning();
        g.drawFittedText (scanning ? "Scanning\n" + PluginHost::get().scanningName()
                                   : "No plugins listed yet.\n\nScan to find the AU and VST3 plugins on this Mac, then double-click one to add it to the selected track.",
                          listArea.reduced (8), juce::Justification::centredTop, 8);
    }
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto r = itemBounds (i).toFloat();
        if (r.getBottom() > (float) bottom)
            break;

        const auto& it = list[(size_t) i];
        const auto c = th.pal (it.pal);
        const bool lit = i == hovered || i == chosen;

        if (tab != 0 && tab != 3)
        {
            // Device card, like the ability cards: framed panel, icon, serif title, numeral
            Glow::drawPanel (g, r, lit ? c : c.withMultipliedAlpha (0.65f), 10.0f, i == chosen ? 1.0f : 0.0f);
            Glow::drawIcon (g, it.icon, r.removeFromLeft (64.0f).reduced (12.0f), c, lit ? 0.8f : 0.0f);
            auto text = r.reduced (2.0f, 12.0f);
            g.setColour (th.text);
            Glow::drawSpacedText (g, it.name.toUpperCase(), text.removeFromTop (text.getHeight() * 0.55f),
                                  th.heading (13.0f), 1.6f, juce::Justification::centredLeft);
            g.setColour (th.muted);
            g.setFont (th.body (11.5f));
            g.drawText (it.detail, text.toNearestInt(), juce::Justification::centredLeft);
            g.setColour (c.withAlpha (0.7f));
            g.setFont (th.heading (11.0f));
            g.drawText (romanNumeral (i + 1), itemBounds (i).reduced (12, 8), juce::Justification::topRight);
        }
        else
        {
            g.setColour (lit ? th.panel2.brighter (0.05f) : th.panel2);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (lit ? c.withAlpha (0.7f) : th.line2);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            Glow::drawIcon (g, it.icon, r.removeFromLeft (34.0f).reduced (8.0f), c);
            g.setColour (th.text);
            g.setFont (th.body (12.5f));
            g.drawText (it.name, r.withTrimmedRight (90).toNearestInt(), juce::Justification::centredLeft);
            g.setColour (th.muted);
            g.setFont (th.label (8.5f));
            g.drawText (it.detail.toUpperCase(), r.withTrimmedRight (8).toNearestInt(), juce::Justification::centredRight);
        }
    }
}
