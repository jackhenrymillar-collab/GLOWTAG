#include "StatusBar.h"

void StatusBar::setInfo (const juce::String& left)
{
    if (left != info)
    {
        info = left;
        repaint();
    }
}

void StatusBar::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto r = getLocalBounds().reduced (14, 0);
    g.setColour (th.pal (Pal::Green));
    g.fillEllipse ((float) r.getX(), (float) r.getCentreY() - 3.0f, 6.0f, 6.0f);
    r.removeFromLeft (14);

    g.setColour (th.muted);
    g.setFont (th.label (9.5f));
    g.drawText (info, r, juce::Justification::centredLeft);
    g.drawText ("SPACE  PLAY     A-K  PLAY NOTES     Z X  OCTAVE     R  RECORD     1 2 3  VIEWS     CMD K  COMMANDS", r, juce::Justification::centredRight);
}
