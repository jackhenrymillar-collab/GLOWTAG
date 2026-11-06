#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Detailed glowing line-art icons, drawn in a 64 x 64 design space
// (ported from the mockup's SVG symbols).
enum class Icon { Sampler, Synth, Drums, Filter, Delay, Reverb, EQ, Compressor, LFO,
                  Pencil, Cursor, Scissors, Velocity, Play, Stop, Record, Loop, Metronome };

namespace Glow
{
    // Stroke a path with a soft neon glow. `intensity` 0..1 brightens it (e.g. with the beat).
    void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour,
                     float width, float intensity = 0.0f);

    // Draw an icon fitted inside `area`.
    void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area,
                   juce::Colour colour, float intensity = 0.0f);

    // Rounded panel with a thin coloured outline and a faint inner frame, like the ability cards.
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour accent,
                    float radius = 14.0f, float pulse = 0.0f);

    // Draw text with extra letter spacing (for serif caps headings and labels)
    void drawSpacedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                         const juce::Font& font, float spacing, juce::Justification just);
}
