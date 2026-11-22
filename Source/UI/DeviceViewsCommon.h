#pragma once

// Shared by the device walls (DeviceViews.cpp, DeviceViewsFx.cpp): formats, the analog envelope and
// filter pictures, and the helpers that tie a neon piece to a stored control.
#include "DeviceViews.h"
#include "../Engine/DeviceParams.h"

namespace DeviceViews
{
    using namespace Neon;
    using juce::Colour;
    using juce::String;
    using P = juce::Point<float>;
    using Cursor = juce::MouseCursor::StandardCursorType;

    namespace C
    {
        const Colour BL (0xff38a8ff), PK (0xffff3fa4), YL (0xffffd23a), CY (0xff3ff0ff), VI (0xffb06bff), OR (0xffff8a3a), GR (0xff4ade80);
        const Colour white = juce::Colours::white;
    }

    // Value words, as the readout shows them
    namespace F
    {
        String time (float s);
        String hz (float f);
        String pct (float v);
        String signedVal (float v, const String& unit, int decimals = 1);
        String db (float gain);
        String note (int n);
    }

    String tipFor (const Device&, int param, const String& label, const std::function<String (float)>& fmt);

    inline float expMap (float v, float lo, float hi) { return Param::expMap (v, lo, hi); }
    inline float inverse (float sec, float lo, float hi) { return Param::inverseExp (sec, lo, hi); }

    // A wall under construction: the device, its group colours and the wall itself
    struct Builder
    {
        Device& d;
        Built& out;
        Wall& wall;

        // A control: a fader bound to a stored value, with its label and readout
        Item& fader (Group&, int param, const String& label, std::function<String (float)> fmt,
                     float x0, float y0, float x1, float y1, std::optional<P> bend = {}, bool isNew = false, bool bipolar = false);
        // A row of words choosing a stored value; `values` default to 0, 1, 2...
        Item& choice (Group&, int param, std::vector<String> words, float x, float y, bool isNew = false, std::vector<int> values = {});
        Item& toggle (Group&, int param, const String& word, float x, float y, bool isNew = false);
        String tip (int param, const String& label, const std::function<String (float)>& fmt) const;

        // The device name, its line of description and the readout of the last control touched
        Group& title (const String& name, float nx, std::function<String()> line);
    };

    // The analog envelope (the same shape as DSP/Analog.h): charges toward 1.25, discharges exponentially
    struct AnalogEnv
    {
        float tA, H, dD, dR, S, A, D, R;
        AnalogEnv (float a, float d, float s, float r, float h = 0.0f);
        float base (float t) const;
        float level (float t, float releasedAt) const;    // releasedAt < 0: still held
    };
    struct EnvPicture
    {
        float xs[6] {}, ws[5] {};
        float y0 = 0, h = 0;
        float Y (float v) const { return y0 + h - v * h; }
        P at (const AnalogEnv&, float t, float releasedAt) const;
    };
    EnvPicture drawEnv (juce::Graphics&, const AnalogEnv&, float x0, float y0, float w, float h, Colour, TubeOptions o = {}, bool clipsSet = false);

    // Filter response pictures. Modes: 0 LP 24, 1 LP 12, 2 BP, 3 HP, 4 LP (12 dB)
    float filterMagnitude (float f, float fc, float res, int mode);
    struct FilterPicture
    {
        float x0, y0, w, h, bx, by, base;
        float X (float f) const { return x0 + std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f) * w; }
        float Y (float m) const { return y0 + h * (1.0f - (juce::jlimit (-48.0f, 24.0f, 20.0f * std::log10 (juce::jmax (1e-6f, m))) + 48.0f) / 72.0f); }
    };
    FilterPicture drawFilter (juce::Graphics&, float x0, float y0, float w, float h, float fcBase, float fcLive, float res, int mode, const Colour* c);

    // The node's latest output as a neon trace, settled on a rising zero crossing
    void scope (juce::Graphics&, const Node*, Colour, float x0, float y0, float w, float h);
    void dashes (juce::Graphics&, const juce::Path&);
    float waveShape (float shape, float t, float pw = 0.5f);    // Saw › Square › Tri › Sine

    // The device walls
    void synthWall (Builder&);
    void samplerWall (Builder&);
    void drumsWall (Builder&);
    void filterWall (Builder&);
    void driveWall (Builder&);
    void delayWall (Builder&);
    void reverbWall (Builder&);
    void eqWall (Builder&);
    void compWall (Builder&);
    void lfoWall (Builder&);
}
