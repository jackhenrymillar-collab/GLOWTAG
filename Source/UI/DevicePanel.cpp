#include "DevicePanel.h"

DeviceCard::DeviceCard (const juce::String& t, Icon i, Pal p, int n,
                        std::initializer_list<std::pair<const char*, double>> knobs)
    : title (t), icon (i), pal (p), numeral (n)
{
    for (auto& [name, value] : knobs)
    {
        auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        const bool bipolar = juce::String (name) == "Pan" || juce::String (name) == "Low" || juce::String (name) == "Mid" || juce::String (name) == "High";
        s->setRange (bipolar ? -1.0 : 0.0, 1.0);
        s->setValue (value, juce::dontSendNotification);
        s->setDoubleClickReturnValue (true, value);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s->getProperties().set ("pal", (int) p);
        s->setTooltip (name);
        addAndMakeVisible (s);
        knobNames.add (name);
    }
}

void DeviceCard::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (44);
    auto row = r.removeFromTop (juce::jmin (r.getHeight(), 70));
    const int n = sliders.size();
    const int cols = n > 4 ? (n + 1) / 2 : n;
    const int w = row.getWidth() / juce::jmax (1, cols);
    for (int i = 0; i < n; ++i)
    {
        if (i == cols)
            row = r.removeFromTop (juce::jmin (r.getHeight(), 70));
        auto cell = row.removeFromLeft (w);
        sliders[i]->setBounds (cell.removeFromTop (cell.getHeight() - 16).withSizeKeepingCentre (48, 48));
    }
}

void DeviceCard::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto c = th.pal (pal);
    auto r = getLocalBounds().toFloat();
    Glow::drawPanel (g, r, c, 12.0f, pulse);

    auto head = r.reduced (12.0f).removeFromTop (36.0f);
    Glow::drawIcon (g, icon, head.removeFromLeft (36.0f), c, pulse);
    head.removeFromLeft (10.0f);
    g.setColour (th.text);
    Glow::drawSpacedText (g, title.toUpperCase(), head, th.heading (14.0f), 2.0f, juce::Justification::centredLeft);
    g.setColour (c.withAlpha (0.75f));
    g.setFont (th.heading (12.0f));
    g.drawText (romanNumeral (numeral), head.toNearestInt(), juce::Justification::centredRight);

    // Knob labels
    g.setColour (th.muted);
    for (int i = 0; i < sliders.size(); ++i)
    {
        auto b = sliders[i]->getBounds();
        Glow::drawSpacedText (g, knobNames[i].toUpperCase(),
                              juce::Rectangle<float> ((float) b.getX() - 20.0f, (float) b.getBottom() + 1.0f, (float) b.getWidth() + 40.0f, 13.0f),
                              th.label (8.0f), 1.2f, juce::Justification::centred);
    }
}

DevicePanel::DevicePanel (AppContext& c) : ctx (c)
{
    cards.add (new DeviceCard ("LFO",     Icon::LFO,     Pal::Violet, 1, { { "Rate", 0.4 }, { "Depth", 0.6 }, { "Shape", 0.2 } }));
    cards.add (new DeviceCard ("Sampler", Icon::Sampler, Pal::Rose,   2, { { "Start", 0.1 }, { "Length", 0.7 }, { "Pitch", 0.5 }, { "Attack", 0.05 }, { "Release", 0.4 }, { "Gain", 0.8 } }));
    cards.add (new DeviceCard ("Filter",  Icon::Filter,  Pal::Sky,    3, { { "Cutoff", 0.65 }, { "Res", 0.3 }, { "Drive", 0.2 } }));
    cards.add (new DeviceCard ("Delay",   Icon::Delay,   Pal::Blue,   4, { { "Time", 0.5 }, { "Feedback", 0.4 }, { "Mix", 0.25 } }));
    cards.add (new DeviceCard ("EQ",      Icon::EQ,      Pal::Gold,   5, { { "Low", 0.1 }, { "Mid", -0.2 }, { "High", 0.3 }, { "Output", 0.7 } }));
    for (auto* card : cards)
        addAndMakeVisible (card);
}

void DevicePanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (24);
    const float weights[] = { 1.0f, 1.7f, 1.0f, 1.0f, 1.25f };
    float total = 0.0f;
    for (auto w : weights) total += w;
    const int gap = 10;
    const float unit = (float) (r.getWidth() - gap * (cards.size() - 1)) / total;
    for (int i = 0; i < cards.size(); ++i)
    {
        cards[i]->setBounds (r.removeFromLeft ((int) (unit * weights[i])));
        r.removeFromLeft (gap);
    }
}

void DevicePanel::tick()
{
    // Cards breathe gently when the selected track plays
    const float m = ctx.selected().meter.load();
    for (auto* card : cards)
        if (std::abs (card->pulse - m) > 0.02f)
        {
            card->pulse = m;
            card->repaint();
        }
}

void DevicePanel::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto head = getLocalBounds().reduced (12, 8).removeFromTop (20).toFloat();
    auto& t = ctx.selected();
    g.setColour (th.muted);
    Glow::drawSpacedText (g, "DEVICE CHAIN", head, th.label (9.0f), 2.0f, juce::Justification::centredLeft);
    g.setColour (th.pal (t.colourIndex));
    Glow::drawSpacedText (g, t.name.toUpperCase(), head.withTrimmedLeft (120.0f), th.heading (13.0f), 2.0f,
                          juce::Justification::centredLeft);
}
