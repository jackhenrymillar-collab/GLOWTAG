#include "GlowLookAndFeel.h"
#include "Icons.h"
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
}

void GlowLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool highlighted, bool down)
{
    auto& th = Theme::get();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const auto accent = accentFor (b, juce::TextButton::buttonOnColourId);

    if (b.getToggleState())
    {
        g.setColour (accent.withAlpha (0.14f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (accent);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }
    else
    {
        g.setColour (highlighted || down ? th.panel2.brighter (0.08f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (r, radius);
        g.setColour (th.line);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }
}

void GlowLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    auto& th = Theme::get();
    g.setColour (b.getToggleState() ? th.text : th.muted);
    Glow::drawSpacedText (g, b.getButtonText().toUpperCase(), b.getLocalBounds().toFloat(),
                          th.label (10.5f), 1.4f, juce::Justification::centred);
}

void GlowLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, juce::Slider& s)
{
    auto& th = Theme::get();
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

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

    auto cap = juce::Rectangle<float> (cx - 15.0f, sliderPos - 9.0f, 30.0f, 18.0f);
    g.setColour (th.line.brighter (0.1f));
    g.fillRoundedRectangle (cap, 5.0f);
    g.setColour (accent.withAlpha (0.7f));
    g.drawRoundedRectangle (cap, 5.0f, 1.0f);
    g.setColour (accent);
    g.drawHorizontalLine ((int) sliderPos, cap.getX() + 5.0f, cap.getRight() - 5.0f);
}
