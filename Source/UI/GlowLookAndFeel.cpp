#include "GlowLookAndFeel.h"
#include "Icons.h"
#include "NeonKit.h"
#include "../Theme.h"

// A component can carry a "pal" property (a Pal index) so its accent follows the theme live.
static juce::Colour accentFor (juce::Component& c, int fallbackColourId)
{
    if (c.getProperties().contains ("pal"))
        return Theme::get().pal ((int) c.getProperties()["pal"]);
    return c.findColour (fallbackColourId);
}

GlowLookAndFeel::GlowLookAndFeel()
{
    refreshColours();
}

void GlowLookAndFeel::refreshColours()
{
    auto& th = Theme::get();
    setColour (juce::ResizableWindow::backgroundColourId, th.bg);
    setColour (juce::TextButton::buttonColourId, th.panel2);
    setColour (juce::TextButton::buttonOnColourId, th.pal (Pal::Green));
    setColour (juce::TextButton::textColourOffId, th.muted);
    setColour (juce::TextButton::textColourOnId, th.text);
    setColour (juce::Slider::rotarySliderFillColourId, th.pal (Pal::Green));
    setColour (juce::Slider::rotarySliderOutlineColourId, th.line2);
    setColour (juce::Slider::textBoxTextColourId, th.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, th.text);
    setColour (juce::TextEditor::backgroundColourId, th.display);
    setColour (juce::TextEditor::outlineColourId, th.line);
    setColour (juce::TextEditor::textColourId, th.text);
    setColour (juce::ScrollBar::thumbColourId, th.line.brighter (0.15f));
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
}

// Buttons are sign words: lit glass when on, grey glass when off, a little light under the pointer.
// No pill, no outline, the same as every control on the device walls.
void GlowLookAndFeel::drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool)
{
}

void GlowLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool down)
{
    auto& th = Theme::get();
    const auto accent = b.getProperties().contains ("pal") ? th.pal ((int) b.getProperties()["pal"]) : th.themeColour();
    const auto r = b.getLocalBounds().toFloat();
    const float size = b.getProperties().contains ("wordSize") ? (float) b.getProperties()["wordSize"]
                                                                : juce::jlimit (12.0f, 20.0f, r.getHeight() * 0.62f);
    const bool on = b.getToggleState() || (bool) b.getProperties()["alwaysLit"];
    const auto text = b.getButtonText().toLowerCase();
    Neon::paintLit (g, b.getWidth(), b.getHeight(), Neon::scratchGlow(), 0.6f, [&] (juce::Graphics& gg, bool)
    {
        Neon::wordButton (gg, text, r, size, accent, on, highlighted || down);
    });
}

void GlowLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, juce::Slider& s)
{
    auto& th = Theme::get();
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Vintage console: a bakelite knob with a pointer line, ringed by the value arc
    if (th.isVintage() && s.getProperties().contains ("console"))
    {
        const auto accent = accentFor (s, juce::Slider::rotarySliderFillColourId);
        juce::Path ring;
        ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::tinted (0xff46474c, accent, 0.22f));
        g.strokePath (ring, juce::PathStrokeType (2.0f));
        const bool bi = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float mid = bi ? (startAngle + endAngle) * 0.5f : startAngle;
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (mid, angle), juce::jmax (mid, angle), true);
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (2.0f));

        const float body = radius - 3.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (centre.x - body, centre.y - body + 1.5f, body * 2.0f, body * 2.0f);
        g.setGradientFill (juce::ColourGradient (Theme::tinted (0xff5c5c5a, accent, 0.14f), centre.x - body * 0.4f, centre.y - body * 0.5f,
                                                 juce::Colour (0xff131414), centre.x + body, centre.y + body, true));
        g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
        g.setColour (Theme::tinted (0xfff2f0e6, accent, 0.35f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (body * 0.25f, angle), centre.getPointOnCircumference (body - 1.0f, angle)), 1.6f);
        return;
    }

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (th.line2);
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar knobs (like pan) fill from the centre
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    Glow::strokeGlow (g, arc, accentFor (s, juce::Slider::rotarySliderFillColourId), 2.4f);

    // pointer dot
    const auto tip = centre.getPointOnCircumference (radius - 7.0f, angle);
    g.setColour (th.text.withAlpha (0.85f));
    g.fillEllipse (tip.x - 1.8f, tip.y - 1.8f, 3.6f, 3.6f);
}

void GlowLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, s);
        return;
    }

    auto& th = Theme::get();
    const auto accent = accentFor (s, juce::Slider::rotarySliderFillColourId);
    const float cx = (float) x + (float) width * 0.5f;

    // dB scale ticks
    g.setColour (th.muted.withAlpha (0.3f));
    for (int i = 0; i <= 10; ++i)
    {
        const float ty = (float) y + (float) height * (float) i / 10.0f;
        g.drawHorizontalLine ((int) ty, cx - 16.0f, cx - 8.0f);
    }

    g.setColour (th.display);
    g.fillRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) height, 3.0f);

    // Vintage console: a chunky two-tone cap with a centre line and a shadow
    if (th.isVintage() && s.getProperties().contains ("console"))
    {
        auto old = juce::Rectangle<float> (cx - 11.0f, sliderPos - 13.0f, 22.0f, 26.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (old.translated (0.0f, 2.5f), 2.5f);
        g.setColour (Theme::tinted (0xffe4e2d8, accent, 0.32f));
        g.fillRoundedRectangle (old, 2.5f);
        g.setColour (Theme::tinted (0xffaeaca2, accent, 0.32f));
        g.fillRoundedRectangle (old.withTop (sliderPos), 2.5f);
        g.setColour (juce::Colour (0xff222323));
        g.fillRect (old.getX(), sliderPos - 1.5f, old.getWidth(), 3.0f);
        g.setColour (juce::Colour (0xff121313));
        g.drawRoundedRectangle (old, 2.5f, 1.0f);
        return;
    }

    auto cap = juce::Rectangle<float> (cx - 15.0f, sliderPos - 9.0f, 30.0f, 18.0f);
    g.setColour (th.line.brighter (0.1f));
    g.fillRoundedRectangle (cap, 5.0f);
    g.setColour (accent.withAlpha (0.7f));
    g.drawRoundedRectangle (cap, 5.0f, 1.0f);
    g.setColour (accent);
    g.drawHorizontalLine ((int) sliderPos, cap.getX() + 5.0f, cap.getRight() - 5.0f);
}
