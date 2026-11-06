#include "BrowserPanel.h"

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
        { "LFO",        "Modulate any knob",      Icon::LFO,        Pal::Violet },
    };

    const ModuleType deviceTypes[] = { ModuleType::Sampler, ModuleType::Synth, ModuleType::Drums, ModuleType::Filter,
                                       ModuleType::Delay, ModuleType::Reverb, ModuleType::EQ, ModuleType::Compressor,
                                       ModuleType::LFO };

    const std::vector<BrowserPanel::Item> samples = {
        { "Kick Tight 01",  "Drums / 0.4 s",  Icon::Drums,   Pal::Rose },
        { "Snare Crack",    "Drums / 0.3 s",  Icon::Drums,   Pal::Green },
        { "Hat Closed 04",  "Drums / 0.1 s",  Icon::Drums,   Pal::Sky },
        { "Clap Stack",     "Drums / 0.5 s",  Icon::Drums,   Pal::Gold },
        { "Vox Hook Am",    "Vocals / 2 bars", Icon::Sampler, Pal::Violet },
        { "Soul Chop 92",   "Loops / 4 bars",  Icon::Sampler, Pal::Amber },
        { "Sub Drop",       "FX / 1.2 s",     Icon::Synth,   Pal::Blue },
        { "Vinyl Crackle",  "Texture / 8 s",  Icon::Sampler, Pal::Ice },
    };

    const std::vector<BrowserPanel::Item> presets = {
        { "Neon Keys",      "Synth",      Icon::Synth,   Pal::Blue },
        { "Brick Sub",      "Synth",      Icon::Synth,   Pal::Amber },
        { "Night Choir",    "Sampler",    Icon::Sampler, Pal::Violet },
        { "Tape Echo",      "Delay",      Icon::Delay,   Pal::Sky },
        { "Alley Verb",     "Reverb",     Icon::Reverb,  Pal::Ice },
        { "Drum Bus Glue",  "Compressor", Icon::Compressor, Pal::Green },
    };
}

BrowserPanel::BrowserPanel (AppContext& c) : ctx (c)
{
    const char* names[] = { "Samples", "Devices", "Presets" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = tabs[i];
        b.setButtonText (names[i]);
        b.setRadioGroupId (2001);
        b.setClickingTogglesState (true);
        b.getProperties().set ("pal", (int) Pal::Green);
        b.onClick = [this, i] { if (tabs[i].getToggleState()) { tab = i; chosen = hovered = -1; repaint(); } };
        addAndMakeVisible (b);
    }
    tabs[tab].setToggleState (true, juce::dontSendNotification);
}

const std::vector<BrowserPanel::Item>& BrowserPanel::items() const
{
    return tab == 0 ? samples : (tab == 1 ? devices : presets);
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (28); // heading
    auto tabRow = r.removeFromTop (28);
    const int w = tabRow.getWidth() / 3;
    for (auto& b : tabs)
        b.setBounds (tabRow.removeFromLeft (w).reduced (2, 0));
    r.removeFromTop (12);
    listArea = r;
}

juce::Rectangle<int> BrowserPanel::itemBounds (int index) const
{
    const int h = tab == 1 ? 60 : 44;
    return { listArea.getX(), listArea.getY() + index * (h + 8), listArea.getWidth(), h };
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
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto r = itemBounds (i).toFloat();
        if (r.getBottom() > (float) getHeight() - 8.0f)
            break;

        const auto& it = list[(size_t) i];
        const auto c = th.pal (it.pal);
        const bool lit = i == hovered || i == chosen;

        if (tab == 1)
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
            Glow::drawIcon (g, it.icon, r.removeFromLeft (44.0f).reduced (11.0f), c);
            g.setColour (th.text);
            g.setFont (th.body (13.0f));
            g.drawText (it.name, r.removeFromTop (r.getHeight() * 0.55f).toNearestInt(), juce::Justification::bottomLeft);
            g.setColour (th.muted);
            g.setFont (th.label (9.5f));
            g.drawText (it.detail.toUpperCase(), r.toNearestInt(), juce::Justification::topLeft);
        }
    }
}
