#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Top bar, all neon: the logo, the arrange / mix / edit words, play stop rec as words,
// the LCD (tempo, signature, position, time), theme swatches and the command button.
class TopBar : public juce::Component
{
public:
    explicit TopBar (AppContext&);

    std::function<void (int)> onModeChange;   // 0 Arrange, 1 Mix, 2 Edit
    std::function<void()> onCommandPalette;
    std::function<void()> onPlayStop;

    void setMode (int mode);
    void tick();                               // called ~30 times a second

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    AppContext& ctx;
    juce::TextButton modeButtons[3];
    IconButton playButton { "play", Icon::Play, Pal::Green };
    IconButton stopButton { "stop", Icon::Stop, Pal::Green };
    IconButton recButton  { "rec", Icon::Record, Pal::Rose };
    IconButton loopButton { "loop", Icon::Loop, Pal::Green };
    IconButton metroButton { "click", Icon::Metronome, Pal::Green };
    Neon::GlowLayer glowLayer;
    juce::TextButton rainbowButton { "Rainbow" };
    juce::TextButton commandButton { "Cmd K" };

    juce::Rectangle<int> logoArea, lcdArea, tempoArea, swatchArea;
    double dragStartBpm = 0.0;

    juce::Rectangle<float> swatchBounds (int index) const;
};
