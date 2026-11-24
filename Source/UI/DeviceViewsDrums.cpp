#include "DeviceViewsCommon.h"
#include "../Engine/Samples.h"
#include "../Engine/DrumKit.h"

// The Drum Rack and the Drum Rack Sequencer walls.
//
// The rack's wall has three zones, told apart by colour in Rainbow: the kit (yellow) holds what changes
// the whole device, the pads (cyan) are what you play, and the chosen pad (pink) holds that pad's own
// controls, each drawn as its own piece of neon: a wave stage with envelope lamps, a stack of level
// lamps, a pitch arc, a filter curve, a drive valve, a snap bolt, a pan see-saw and choke chain links.
//
// The sequencer's wall: its groove (yellow), all sixteen rows at once (cyan), and the chosen row
// opened up with its velocity, chance and ratchet lanes (pink).
namespace DeviceViews
{
namespace
{
    float clampf (float v, float lo = 0.0f, float hi = 1.0f) { return juce::jlimit (lo, hi, v); }
    constexpr float pi = juce::MathConstants<float>::pi;

    // Pad colours by range
    const Colour lowC[]  = { Colour (0xffff3b3b), Colour (0xffff6a2a), Colour (0xffff9a2e), Colour (0xffffc23a), Colour (0xffffe06a), Colour (0xffe8364f) };
    const Colour midC[]  = { Colour (0xffc77dff), Colour (0xffff3fa4), Colour (0xffff6ec7), Colour (0xffe05cff), Colour (0xff9b6bff) };
    const Colour highC[] = { Colour (0xff38a8ff), Colour (0xff4ade80), Colour (0xff3ff0ff), Colour (0xffeef6ff), Colour (0xff5cffc8), Colour (0xff8fd0ff) };

    const char* rangeWord (DrumKit::Range r) { return r == DrumKit::Range::Low ? "low" : r == DrumKit::Range::High ? "high" : "mid"; }

    TubeOptions opts (float width, bool off = false, int style = 0)
    {
        TubeOptions o;
        o.w = width; o.off = off; o.style = style;
        return o;
    }

    // A zone: a thin unlit frame, its name lit and a quieter line beside it
    float zone (juce::Graphics& g, float w, float h, const Colour* c, const String& title, const String& sub, float size = 22.0f)
    {
        TubeOptions o = opts (0.55f, true);
        o.closed = true;
        tube (g, roundRect (0, 0, w, h, 16), c[0], o);
        const float tw = text (g, title, 18, 32, size, c[0], 0.95f);
        if (sub.isNotEmpty()) text (g, sub, 18 + tw + 14, 32, 14, c[2], 0.42f);
        return tw;
    }

    // A pad's picture: its sample's peaks, or the shape of its analogue voice
    struct Pic { std::array<float, 64> pk {}; float sec = 0.5f; };
    Pic voicePicture (int voice, float a, float bb, float cc)
    {
        using namespace Param::Drums;
        Pic p;
        float k = 5.0f;
        switch (voice)
        {
            case Kick:  p.sec = 0.6f; k = 4.0f; break;
            case Snare: p.sec = 0.35f; k = 6.0f + (1.0f - cc) * 4.0f; break;
            case Hat:   p.sec = juce::jmax (0.08f, 3.0f * (0.025f + (1.0f - cc) * 0.5f)); k = 4.0f; break;
            case Clap:  p.sec = 0.45f; k = 3.0f + (1.0f - cc) * 5.0f; break;
            case Tom:   p.sec = 0.8f; k = 4.0f; break;
            case Perc:  p.sec = 0.35f; k = 3.0f + (1.0f - cc) * 8.0f; break;
            default: break;
        }
        for (int j = 0; j < 64; ++j)
        {
            const float u = (float) j / 63.0f;
            float v = std::exp (-u * k);
            if (voice == Clap) v = (j < 3 || (j > 4 && j < 7) || (j > 8 && j < 11)) ? 0.9f : v * 0.75f;
            if (voice == Snare || voice == Hat || voice == Clap) v *= 0.75f + 0.25f * std::sin ((float) j * 2.7f + a * 5.0f + bb);
            p.pk[(size_t) j] = clampf (v * 0.85f);
        }
        return p;
    }

    struct DrumState
    {
        std::array<String, 16> names;
        std::array<Pic, 16> samples;               // each pad's sample, as it was when the wall was built
        std::array<float, 16> lastAge {};
        std::array<int, 16> lastPitch {};
        struct Ripple { int pad; double t0; float vel; };
        std::vector<Ripple> ripples;
        int dropPad = -1;
        juce::uint32 lastVoiceKey = 0;
        std::unique_ptr<juce::FileChooser> chooser;

        void load (Device& d)
        {
            auto* m = d.module();
            if (m == nullptr) return;
            for (int i = 0; i < 16; ++i)
            {
                names[(size_t) i] = DrumKit::padName (*m, i);
                const auto& nm = m->padNames[(size_t) i].isNotEmpty() ? m->padNames[(size_t) i] : Samples::defaultPads()[(size_t) i];
                auto& pic = samples[(size_t) i];
                if (auto data = d.ctx.engine.resolveSample (nm))
                {
                    pic.sec = juce::jmax (0.01f, (float) data->lengthSeconds());
                    for (int j = 0; j < 64; ++j)
                        pic.pk[(size_t) j] = data->peaks.empty() ? 0.0f : data->peaks[(size_t) juce::jlimit (0, (int) data->peaks.size() - 1, j * (int) data->peaks.size() / 64)];
                }
            }
            lastPitch.fill (-1);
        }
        Pic picture (const Device& d, int pad) const
        {
            using namespace Param::Drums;
            const int v = d.choice (Voice0 + pad);
            if (v <= Sample || v >= numVoices) return samples[(size_t) pad];
            return voicePicture (v, d.get (VoiceA0 + pad), d.get (VoiceB0 + pad), d.get (VoiceC0 + pad));
        }
        String name (const Device& d, int pad) const
        {
            using namespace Param::Drums;
            const int v = d.choice (Voice0 + pad);
            if (v > Sample && v < numVoices) return String ("analogue ") + voiceName (v);
            return names[(size_t) pad].toLowerCase();
        }

        // New hits, for the ripples: a voice that started again since the last frame
        void watch (const Node* node)
        {
            if (node == nullptr) return;
            const double t = now();
            for (int i = 0; i < 16; ++i)
            {
                const auto& v = node->views[(size_t) i];
                const int pitch = v.pitch.load();
                const float age = v.age.load();
                if (pitch >= 0 && (lastPitch[(size_t) i] < 0 || age + 1e-4f < lastAge[(size_t) i]))
                    ripples.push_back ({ i, t, v.velocity.load() });
                lastPitch[(size_t) i] = pitch;
                lastAge[(size_t) i] = age;
            }
            ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [t] (const Ripple& r) { return t - r.t0 > 0.65; }), ripples.end());
        }
    };

    String signedSt (float v, float range) { return F::signedVal ((float) juce::roundToInt ((v - 0.5f) * range), " st", 0); }

    // The two filter shapes the pictures draw (the pad's filter is a 12 dB state variable filter)
    float padFilter (float f, float fc, float res, int mode)
    {
        const float x = f / fc, q = 0.6f + res * 7.0f, den = juce::jmax (0.02f, std::hypot (1.0f - x * x, x / q));
        return mode == Param::Drums::HP ? x * x / den : mode == Param::Drums::BP ? (x / q) / den * 1.6f : 1.0f / den;
    }

    Line ellipse (float cx, float cy, float rx, float ry, int n = 28)
    {
        Line l;
        for (int i = 0; i <= n; ++i) { const float a = (float) i / (float) n * 2.0f * pi; l.push_back ({ cx + std::cos (a) * rx, cy + std::sin (a) * ry }); }
        return l;
    }

    void fillGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius, Colour c, float lv)
    {
        juce::ColourGradient grad (rgba (c, 0.5f * lv), r.getX(), r.getBottom(), rgba (c, 0.0f), r.getX(), r.getY(), false);
        grad.addColour (clampf (lv), rgba (c, 0.22f * lv));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, radius);
    }
}

//==============================================================================
// Pad colours, row order, the rack a sequencer plays

std::array<Colour, 16> padColours (const Module& rack)
{
    std::array<Colour, 16> out {};
    if (! look().padColours || rack.type != ModuleType::Drums) return out;
    int n[3] = { 0, 0, 0 };
    for (int i = 0; i < 16; ++i)
    {
        const auto r = DrumKit::rangeOf (rack, i);
        const int k = (int) r;
        out[(size_t) i] = r == DrumKit::Range::Low ? lowC[n[k] % 6] : r == DrumKit::Range::Mid ? midC[n[k] % 5] : highC[n[k] % 6];
        ++n[k];
    }
    return out;
}

const Module* drumRackFor (const Device& sequencer)
{
    const auto& patch = sequencer.ctx.project.slot (sequencer.track).patch;
    for (auto& c : patch.cables)
        if (c.fromModule == sequencer.moduleId && c.fromPort == Port::notesOut)
            if (const auto* m = patch.find (c.toModule); m != nullptr && m->type == ModuleType::Drums)
                return m;
    return nullptr;
}

RowOrder rowOrder (const Module* rack)
{
    RowOrder o;
    std::array<int, 16> idx {};
    for (int i = 0; i < 16; ++i) idx[(size_t) i] = i;
    std::array<int, 16> range {};
    const bool grouped = rack != nullptr && look().padColours;
    for (int i = 0; i < 16; ++i) range[(size_t) i] = grouped ? (int) DrumKit::rangeOf (*rack, i) : 0;
    std::stable_sort (idx.begin(), idx.end(), [&] (int a, int b) { return range[(size_t) a] < range[(size_t) b]; });
    for (int n = 0; n < 16; ++n)
    {
        o.rowAt[(size_t) n] = idx[(size_t) n];
        o.posOf[(size_t) idx[(size_t) n]] = n;
        o.groupOf[(size_t) idx[(size_t) n]] = range[(size_t) idx[(size_t) n]];
    }
    return o;
}

//==============================================================================
// Drum Rack

void drumsWall (Builder& b)
{
    using namespace Param::Drums;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::YL; b.out.pal[1] = C::PK; b.out.pal[2] = C::CY;
    b.out.parts = { C::OR, C::YL, C::CY, C::PK };
    enum { Kit = 1, Pads, PadZone };

    auto st = std::make_shared<DrumState>();
    st->load (*dev);
    auto sel = [dev] { return juce::jlimit (0, 15, dev->choice (Selected)); };
    auto colours = [dev] { auto* m = dev->module(); return m != nullptr ? padColours (*m) : std::array<Colour, 16> {}; };
    auto pick = [] (const std::array<Colour, 16>& pc, int i, Colour fallback) { return pc[(size_t) i].isTransparent() ? fallback : pc[(size_t) i]; };
    auto silent = [dev] (int i)
    {
        bool solo = false;
        for (int p = 0; p < 16; ++p) solo = solo || dev->on (Solo0 + p);
        return dev->on (Mute0 + i) || (solo && ! dev->on (Solo0 + i));
    };
    auto changed = [dev] { if (dev->patchChanged) dev->patchChanged(); };

    // The title, and the pad colours switch
    {
        auto& g = wall.add (0, 48, 22, 0, 210, 80, 330);
        auto& pc = g.toggle ("pad colours", 432, 176, [] { return look().padColours; },
                             [w] (bool v) { look().padColours = v; if (look().changed) look().changed(); w->say (v ? "pads coloured by what they play: low warm, mid pink, high cool" : "pads in the device's own colours"); });
        pc.size = 17; pc.ci = 1; pc.isNew = true;
        g.draw = [w] (juce::Graphics& gr, double, float, const Colour* c)
        {
            bigTag (gr, "Drum Rack", 0, 96, c[0]);
            text (gr, "sixteen pads and analogue voices, its sequencer next door", 6, 140, 18, c[1], 0.8f);
            const double age = now() - w->readoutTime;
            if (age < 3.5) text (gr, w->readout, 6, 176, 19, C::white, clampf (1.2f - (float) age / 3.0f) * 0.65f);
        };
    }

    // The kit: everything that changes the whole device
    {
        auto& g = wall.add (Kit, 640, 22, 0, 460, 95, 520);
        auto loadKit = [dev, w, changed] (int delta)
        {
            auto* m = dev->module();
            if (m == nullptr) return;
            const auto kits = DrumKit::list();
            if (kits.empty()) return;
            const int at = DrumKit::indexOf (kits, m->kitName), n = (int) kits.size();
            const int next = at < 0 ? (delta > 0 ? 0 : n - 1) : ((at + delta) % n + n) % n;
            if (DrumKit::load (*m, kits[(size_t) next]))
            {
                dev->ctx.touched();
                w->say ("kit   " + kits[(size_t) next].name);
                changed();
            }
        };
        auto& prev = g.toggle ("<", 214, 32, [] { return true; }, [loadKit] (bool) { loadKit (-1); });
        prev.size = 24; prev.tip = [] { return String ("the kit before"); };
        auto& next = g.toggle (">", 470, 32, [] { return true; }, [loadKit] (bool) { loadKit (1); });
        next.size = 24; next.tip = [] { return String ("the next kit"); };
        auto& save = g.toggle ("save kit", 530, 32, [] { return true; }, [dev, w] (bool)
        {
            auto* m = dev->module();
            if (m == nullptr) return;
            const auto name = m->kitName.isEmpty() || m->kitName == "factory" || m->kitName == "analogue" ? DrumKit::nextFreeName ("kit") : m->kitName;
            if (DrumKit::save (*m, name).existsAsFile()) { m->kitName = name; dev->ctx.touched(); w->say ("saved as   " + name); }
            else w->say ("couldn't save the kit");
        });
        save.size = 18; save.ci = 1; save.isNew = true;
        save.tip = [] { return "save these sixteen pads as a kit   " + DrumKit::folder().getFullPathName(); };
        auto& fill = g.toggle ("fill from folder", 650, 32, [] { return true; }, [dev, st, w, changed] (bool)
        {
            st->chooser = std::make_unique<juce::FileChooser> ("Fill the pads from a folder of samples", juce::File());
            st->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                      [dev, w, changed] (const juce::FileChooser& fc)
            {
                auto* m = dev->module();
                const auto dir = fc.getResult();
                if (m == nullptr || ! dir.isDirectory()) return;
                const int n = DrumKit::fillFromFolder (*m, dir);
                w->say (n > 0 ? String (n) + " pads filled from " + dir.getFileName().toLowerCase() : String ("no samples in that folder"));
                if (n > 0) { dev->ctx.touched(); changed(); }
            });
        });
        fill.size = 18; fill.ci = 1; fill.isNew = true;
        fill.tip = [] { return String ("choose a folder: its samples go onto the pads, low sounds first"); };

        struct K { int param; const char* label; bool bipolar, isNew; std::function<String (float)> fmt; };
        const K faders[] = {
            { Tune, "tune", true, false, [] (float v) { return signedSt (v, 24.0f); } },
            { Gain, "gain", false, false, [] (float v) { return F::db (v / 0.8f); } },
            { Decay, "decay", false, false, [] (float v) { return v >= 0.99f ? String ("rings out") : F::pct (v); } },
            { Snap, "snap", false, false, [] (float v) { return F::pct (v); } },
            { Dirt, "dirt", false, false, [] (float v) { return F::pct (v); } },
            { VelSens, "velocity", false, true, [] (float v) { return F::pct (v); } } };
        for (int j = 0; j < 6; ++j)
        {
            const float x0 = 24.0f + (float) j * 148.0f, x1 = x0 + 124.0f;
            auto& it = b.fader (g, faders[j].param, faders[j].label, faders[j].fmt, x0, 136, x1, 136, P ((x0 + x1) * 0.5f, 136.0f + (j % 2 ? 9.0f : -9.0f)), faders[j].isNew, faders[j].bipolar);
            const auto label = String (faders[j].label);
            const auto fmt = faders[j].fmt;
            const int param = faders[j].param;
            it.tip = [dev, label, fmt, param] { return "kit " + label + "   " + fmt (dev->get (param)); };
        }

        // No sequencer in front of this rack yet: one word adds it
        auto hasSeq = [dev]
        {
            const auto& patch = dev->ctx.project.slot (dev->track).patch;
            const int into = patch.cableInto (dev->moduleId, Port::notesIn);
            const auto* src = into >= 0 ? patch.find (patch.cables[(size_t) into].fromModule) : nullptr;
            return src != nullptr && src->type == ModuleType::DrumSeq;
        };
        auto& add = g.toggle ("add a drum rack sequencer", 24, 82, [] { return false; }, [dev, w, changed] (bool)
        {
            dev->ctx.project.slot (dev->track).patch.addSequencerFor (dev->moduleId);
            dev->ctx.touched();
            w->say ("a drum rack sequencer now plays this rack");
            changed();
        });
        add.size = 16; add.ci = 2; add.isNew = true;
        add.visible = [hasSeq] { return ! hasSeq(); };
        add.tip = [] { return String ("put a Drum Rack Sequencer in front of this rack"); };

        g.draw = [dev, hasSeq] (juce::Graphics& gr, double, float, const Colour* c)
        {
            zone (gr, 920, 184, c, "kit", "the whole device");
            auto* m = dev->module();
            const String name = m != nullptr && m->kitName.isNotEmpty() ? m->kitName.toLowerCase() : String ("factory");
            text (gr, name, 342, 32, 20, c[1], 0.9f, Align::centre);
            if (hasSeq()) text (gr, "its steps come from the drum rack sequencer", 24, 82, 14, c[2], 0.4f);
        };
    }

    // The pads
    const float PW = 136, PH = 148, GP = 12;
    auto padPos = [=] (int i) { return P (16.0f + (float) (i % 4) * (PW + GP), 46.0f + (float) (3 - i / 4) * (PH + GP)); };
    auto padAt = [=] (float lx, float ly)
    {
        for (int i = 0; i < 16; ++i)
        {
            const auto p = padPos (i);
            if (lx >= p.x && lx < p.x + PW && ly >= p.y && ly < p.y + PH) return i;
        }
        return -1;
    };
    {
        static const char* keymap = "awsedftgyhujkolp";
        auto& g = wall.add (Pads, 40, 214, 0, 300, 350, 420);
        auto& a = g.dragArea ({ 16, 46, 4 * PW + 3 * GP, 4 * PH + 3 * GP }, Cursor::PointingHandCursor);
        a.tip = [dev, st, sel] { return "pad " + String (sel() + 1) + "   " + st->name (*dev, sel()); };
        a.down = [dev, st, w, padAt, padPos, PW, PH] (float lx, float ly) -> std::optional<Gesture>
        {
            const int i = padAt (lx, ly);
            if (i < 0) return Gesture {};
            const auto p = padPos (i);
            const auto nm = st->name (*dev, i);
            if (ly < p.y + 36 && lx > p.x + PW - 52)
            {
                const bool solo = lx >= p.x + PW - 28;
                const int param = (solo ? Solo0 : Mute0) + i;
                const bool v = ! dev->on (param);
                dev->setRaw (param, v ? 1.0f : 0.0f);
                w->say (nm + (solo ? (v ? "   solo" : "   solo off") : (v ? "   muted" : "   unmuted")));
                return Gesture {};
            }
            dev->setRaw (Selected, (float) i);
            const float vel = clampf (1.0f - (ly - p.y) / PH * 0.8f, 0.2f, 1.0f);
            dev->note (36 + i, true, vel);
            w->say (nm + "   velocity " + String (juce::roundToInt (vel * 127.0f)));
            Gesture gs;
            gs.up = [dev, i] { dev->note (36 + i, false); };
            return gs;
        };
        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            zone (gr, 612, 696, c, "pads", "hit high for loud, drop samples on a pad");
            const auto* node = dev->live();
            st->watch (node);
            const auto pc = colours();
            const int s = sel();
            const double t = now();
            for (int i = 0; i < 16; ++i)
            {
                const auto p = padPos (i);
                const bool me = i == s, dead = silent (i);
                const auto col = pick (pc, i, c[me ? 1 : 0]);
                const float lv = node != nullptr ? clampf (node->views[(size_t) i].level.load() * (0.4f + 0.9f * node->views[(size_t) i].velocity.load())) : 0.0f;
                if (lv > 0.03f) fillGlass (gr, { p.x + 3, p.y + 3, PW - 6, PH - 6 }, 12, col, lv);

                const auto pic = st->picture (*dev, i);
                const bool rev = dev->on (Reverse0 + i);
                juce::Path wave;
                for (int j = 0; j < 32; ++j)
                {
                    const float pk = juce::jmax (1.0f, pic.pk[(size_t) (rev ? 63 - j * 2 : j * 2)] * 28.0f * (0.7f + 0.3f * lv));
                    const float xx = p.x + 14 + (float) j * (PW - 28) / 31.0f;
                    wave.startNewSubPath (xx, p.y + 84 - pk); wave.lineTo (xx, p.y + 84 + pk);
                }
                gr.setColour (mix (col, C::white, 0.25f + 0.5f * lv).withAlpha (dead ? 0.2f : 0.45f + 0.55f * lv));
                gr.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                if (lv > 0.05f) glow().add (wave, rgba (col, 0.35f * lv), 1.8f);

                text (gr, st->name (*dev, i), p.x + 12, p.y + 26, 16, col, dead ? 0.0f : me ? 0.95f : 0.62f);
                bulb (gr, p.x + PW - 40, p.y + 18, c[2], dev->on (Mute0 + i) ? 1.0f : 0.0f, 5);
                bulb (gr, p.x + PW - 16, p.y + 18, c[1], dev->on (Solo0 + i) ? 1.0f : 0.0f, 5);
                label (gr, "m", p.x + PW - 44, p.y + 38, Align::left, Colour (0xff5c5e66), 12);
                label (gr, "s", p.x + PW - 20, p.y + 38, Align::left, Colour (0xff5c5e66), 12);
                float bx = p.x + 12;
                const int voice = dev->choice (Voice0 + i);
                if (voice > Sample) bx += text (gr, voiceName (voice), bx, p.y + PH - 12, 13, col, dead ? 0.0f : 0.55f) + 8;
                if (const int ch = dev->choice (Choke0 + i); ch > 0)
                    bx += text (gr, String ("choke ") + "abc"[juce::jlimit (1, 3, ch) - 1], bx, p.y + PH - 12, 13, c[2], dead ? 0.0f : 0.4f) + 8;
                if (dev->choice (Out0 + i) == 1) text (gr, "own out", bx, p.y + PH - 12, 13, c[2], dead ? 0.0f : 0.4f);
                label (gr, String::charToString ((juce::juce_wchar) keymap[i]), p.x + PW - 18, p.y + PH - 12);

                for (auto& r : st->ripples)
                    if (r.pad == i)
                    {
                        const float u = (float) ((t - r.t0) / 0.65), e = u * 16.0f * r.vel;
                        gr.setColour (rgba (col, (1.0f - u) * 0.7f * r.vel));
                        gr.drawRoundedRectangle (p.x - e, p.y - e, PW + 2 * e, PH + 2 * e, 14 + e, 2.0f);
                    }
                TubeOptions o = opts (me ? 1.3f : 0.75f, dead && ! me, me ? -1 : (look().style == 1 ? 1 : 0));
                o.closed = true;
                tube (gr, roundRect (p.x, p.y, PW, PH, 14), me ? mix (col, C::white, 0.15f) : col, o);
                if (st->dropPad == i)
                {
                    juce::Path box;
                    box.addRectangle (p.x - 5, p.y - 5, PW + 10, PH + 10);
                    gr.setColour (C::white.withAlpha (0.8f));
                    dashes (gr, box);
                }
            }
        };
    }

    // Files dropped on a pad
    b.out.dropHover = [st, padAt] (std::optional<P> at)
    {
        st->dropPad = at ? padAt (at->x - 40, at->y - 214) : -1;
    };
    b.out.dropFiles = [dev, st, w, padAt, changed] (P at, const juce::StringArray& files)
    {
        st->dropPad = -1;
        auto* m = dev->module();
        const int i = padAt (at.x - 40, at.y - 214);
        if (m == nullptr || i < 0) return false;
        int n = 0;
        for (auto& f : files)
        {
            if (! juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) continue;
            const int pad = i + n;
            if (pad > 15) break;
            m->padNames[(size_t) pad] = f;
            m->knobValues[(size_t) (Voice0 + pad)] = (float) Sample;
            ++n;
        }
        if (n == 0) return false;
        m->knobValues[(size_t) Selected] = (float) i;
        dev->ctx.touched();
        w->say (n == 1 ? juce::File (files[0]).getFileNameWithoutExtension().toLowerCase() + " on pad " + String (i + 1)
                       : String (n) + " samples from pad " + String (i + 1));
        changed();
        return true;
    };

    // The chosen pad: its own controls, each drawn its own way
    {
        auto& g = wall.add (PadZone, 666, 214, 0, 450, 330, 560);
        const float SX = 20, SW = 540, mid = 214, amp = 76;
        struct M { Pic pic; float sec, st, aT, hT, tau, rate; };
        auto tm = [dev, st, sel] ()
        {
            const int p = sel();
            M m { st->picture (*dev, p), 0, 0, 0, 0, 0, 0 };
            m.sec = m.pic.sec;
            m.st = dev->get (Start0 + p) * m.sec;
            m.aT = attackSec (dev->get (Attack0 + p));
            m.hT = holdSec (dev->get (Hold0 + p));
            m.tau = decayTau (dev->get (PDecay0 + p));
            const float kd = dev->get (Decay);
            m.rate = kd >= 0.99f ? 0.0f : (1.0f - kd) * 30.0f;
            return m;
        };
        auto X = [SX, SW] (const M& m, float s) { return SX + clampf (s / m.sec) * SW; };
        auto env = [] (const M& m, float t)
        {
            if (t < 0) return 0.0f;
            float e = t < m.aT ? t / m.aT : (t < m.aT + m.hT || m.tau <= 0.0f ? 1.0f : std::exp (-(t - m.aT - m.hT) / m.tau));
            if (m.rate > 0.0f) e *= std::exp (-t * m.rate);
            return e;
        };
        auto padParam = [sel] (int base) { return base + sel(); };
        auto nameNow = [dev, st, sel] { return st->name (*dev, sel()); };

        auto& mute = g.toggle ("mute", 734, 32, [dev, sel] { return dev->on (Mute0 + sel()); },
                               [dev, sel, w] (bool v) { dev->setRaw (Mute0 + sel(), v ? 1.0f : 0.0f); w->say (v ? "muted" : "unmuted"); });
        mute.size = 19; mute.ci = 1; mute.isNew = true;
        auto& solo = g.toggle ("solo", 812, 32, [dev, sel] { return dev->on (Solo0 + sel()); },
                               [dev, sel, w] (bool v) { dev->setRaw (Solo0 + sel(), v ? 1.0f : 0.0f); w->say (v ? "solo" : "solo off"); });
        solo.size = 19; solo.ci = 1; solo.isNew = true;

        std::vector<std::pair<int, String>> voiceWords;
        for (int v = 0; v < numVoices; ++v) voiceWords.push_back ({ v, voiceName (v) });
        auto& voice = g.words (voiceWords, 20, 76);
        voice.pre = "voice"; voice.size = 19; voice.gap = 18; voice.isNew = true;
        voice.getChoice = [dev, sel] { return dev->choice (Voice0 + sel()); };
        voice.setChoice = [dev, sel, w] (int v)
        {
            dev->setRaw (Voice0 + sel(), (float) v);
            static const char* what[] = { "", "a sine that drops, with a click", "two drum heads and filtered noise", "six square waves, high passed",
                                          "bursts of noise and a room", "a sine that sweeps, with skin noise", "two squares through a band pass" };
            w->say (v == Sample ? String ("plays its sample") : String ("analogue ") + voiceName (v) + "   " + what[juce::jlimit (0, 6, v)]);
        };

        // The envelope's lamps on the wave stage
        auto& atk = g.handle ([=] { const auto m = tm(); return std::optional<P> (P (X (m, m.st + m.aT), mid - amp - 14)); });
        atk.ci = 1; atk.r = 6.5f; atk.isNew = true;
        atk.start = [dev, padParam] { return std::vector<float> { dev->get (padParam (Attack0)) }; };
        atk.drag = [dev, padParam] (float dx, float, const std::vector<float>& s) { dev->set (padParam (Attack0), s[0] + dx / 300.0f); };
        atk.tip = [dev, padParam] { return "attack   " + F::time (attackSec (dev->get (padParam (Attack0)))); };
        atk.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (Attack0), 0.0f); };
        auto& hold = g.handle ([=] { const auto m = tm(); return std::optional<P> (P (X (m, m.st + m.aT + m.hT) + (m.hT < 0.002f ? 16.0f : 0.0f), mid - amp - 14)); });
        hold.ci = 1; hold.r = 5.0f;
        hold.start = [dev, padParam] { return std::vector<float> { dev->get (padParam (Hold0)) }; };
        hold.drag = [dev, padParam] (float dx, float, const std::vector<float>& s) { dev->set (padParam (Hold0), s[0] + dx / 300.0f); };
        hold.tip = [dev, padParam] { return "hold   " + F::time (holdSec (dev->get (padParam (Hold0)))); };
        hold.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (Hold0), 0.0f); };
        auto& dec = g.handle ([=]
        {
            const auto m = tm();
            if (m.tau <= 0.0f) return std::optional<P> (P (SX + SW - 8, mid - amp * env (m, m.sec - m.st) - 14));
            const float td = m.st + m.aT + m.hT + m.tau * 1.2f;
            return std::optional<P> (P (X (m, td), mid - amp * env (m, td - m.st) - 14));
        });
        dec.ci = 1; dec.r = 6.5f;
        dec.start = [dev, padParam] { return std::vector<float> { dev->get (padParam (PDecay0)) }; };
        dec.drag = [dev, padParam] (float dx, float dy, const std::vector<float>& s) { dev->set (padParam (PDecay0), s[0] + dx / 340.0f - dy / 560.0f); };
        dec.tip = [dev, padParam] { const float v = dev->get (padParam (PDecay0)); return "decay   " + (v >= 0.99f ? String ("rings out") : F::time (decayTau (v) * 3.0f)); };
        dec.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (PDecay0), 1.0f); };
        auto& start = g.handle ([=] { const auto m = tm(); return std::optional<P> (P (X (m, m.st), mid + amp + 22)); });
        start.ci = 2; start.r = 6.5f; start.isNew = true;
        start.start = [dev, padParam] { return std::vector<float> { dev->get (padParam (Start0)) }; };
        start.drag = [dev, padParam, SW] (float dx, float, const std::vector<float>& s) { dev->set (padParam (Start0), clampf (s[0] + dx / SW, 0.0f, 0.95f)); };
        start.tip = [=] { const auto m = tm(); return "start   " + F::time (m.st); };
        start.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (Start0), 0.0f); };
        start.visible = [dev, sel] { return dev->choice (Voice0 + sel()) == Sample; };

        auto& rev = g.toggle ("reverse", 20, 350, [dev, padParam] { return dev->on (padParam (Reverse0)); },
                              [dev, padParam, w] (bool v) { dev->setRaw (padParam (Reverse0), v ? 1.0f : 0.0f); w->say (v ? "plays backwards" : "plays forwards"); });
        rev.size = 19; rev.ci = 2; rev.isNew = true;
        rev.visible = [dev, sel] { return dev->choice (Voice0 + sel()) == Sample; };

        // The analogue voice's three controls, named for the voice
        std::array<Item*, 3> voiceFaders {};
        for (int j = 0; j < 3; ++j)
        {
            const float x0 = 20.0f + (float) j * 184.0f, x1 = x0 + 150.0f;
            auto& f = g.fader (x0, 452, x1, 452, P ((x0 + x1) * 0.5f, 452.0f + (j == 1 ? 10.0f : -10.0f)));
            const int base = j == 0 ? VoiceA0 : j == 1 ? VoiceB0 : VoiceC0;
            f.def = 0.5f; f.isNew = true;
            f.get = [dev, sel, base] { return dev->get (base + sel()); };
            f.set = [dev, sel, base] (float v) { dev->set (base + sel(), v); };
            f.visible = [dev, sel] { return dev->choice (Voice0 + sel()) != Sample; };
            f.tip = [dev, sel, base, j]
            {
                const int v = dev->choice (Voice0 + sel());
                return String (voiceName (v)) + "   " + voiceParam (v, j) + "   " + F::pct (dev->get (base + sel()));
            };
            voiceFaders[(size_t) j] = &f;
        }

        // Level: a tall stack of lamps
        auto& lvl = g.dragArea ({ 594, 104, 36, 240 }, Cursor::UpDownResizeCursor);
        lvl.tip = [dev, padParam] { return "level   " + F::db (dev->get (padParam (Level0)) / 0.8f); };
        lvl.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (Level0), 0.8f); };
        lvl.down = [dev, padParam] (float, float ly) -> std::optional<Gesture>
        {
            auto set = [dev, padParam] (float y) { dev->set (padParam (Level0), (338.0f - y) / 226.0f); };
            set (ly);
            Gesture gs; gs.move = [set] (float, float y) { set (y); };
            return gs;
        };

        // Pitch: an arc of semitone ticks, two octaves each way
        const P PC (758, 300);
        const float PR = 106, A0 = pi * 1.15f, AS = pi * 0.7f;
        auto& pitch = g.dragArea ({ 654, 166, 240, 150 }, Cursor::PointingHandCursor);
        pitch.tip = [dev, padParam] { return "pitch   " + signedSt (dev->get (padParam (Pitch0)), 48.0f); };
        pitch.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (Pitch0), 0.5f); };
        pitch.down = [dev, padParam, PC, A0, AS] (float lx, float ly) -> std::optional<Gesture>
        {
            auto set = [dev, padParam, PC, A0, AS] (float x, float y)
            {
                float a = std::atan2 (y - PC.y, x - PC.x);
                if (a < 0) a += 2 * pi;
                if (a < pi * 0.5f) a = 2 * pi;
                dev->set (padParam (Pitch0), (float) juce::roundToInt (clampf ((a - A0) / AS) * 48.0f) / 48.0f);
            };
            set (lx, ly);
            Gesture gs; gs.move = set;
            return gs;
        };

        // The filter: a lamp on its curve sets cutoff (across) and resonance (up)
        const float FX0 = 20, FY0 = 500, FW = 240, FH = 130;
        auto Xf = [=] (float f) { return FX0 + std::log (f / 20.0f) / std::log (1000.0f) * FW; };
        auto Yf = [=] (float m) { return FY0 + FH * (1.0f - (juce::jlimit (-30.0f, 18.0f, 20.0f * std::log10 (juce::jmax (1e-6f, m))) + 30.0f) / 48.0f); };
        auto& fh = g.handle ([=]
        {
            const int p = sel();
            const float fc = cutoffHz (dev->get (Cutoff0 + p));
            return std::optional<P> (P (Xf (fc), Yf (padFilter (fc, fc, dev->get (Res0 + p), dev->choice (FMode0 + p)))));
        });
        fh.ci = 1; fh.r = 6.5f; fh.isNew = true;
        fh.start = [dev, sel] { return std::vector<float> { dev->get (Cutoff0 + sel()), dev->get (Res0 + sel()) }; };
        fh.drag = [dev, sel, FW] (float dx, float dy, const std::vector<float>& s) { dev->set (Cutoff0 + sel(), s[0] + dx / FW); dev->set (Res0 + sel(), s[1] - dy / 120.0f); };
        fh.tip = [dev, sel]
        {
            static const char* modes[] = { "lp", "bp", "hp" };
            const int p = sel();
            return String ("filter ") + modes[juce::jlimit (0, 2, dev->choice (FMode0 + p))] + "   " + F::hz (cutoffHz (dev->get (Cutoff0 + p))) + "   res " + F::pct (dev->get (Res0 + p));
        };
        fh.doubleClick = [dev, sel] (float, float) { dev->set (Cutoff0 + sel(), 1.0f); dev->set (Res0 + sel(), 0.0f); };
        auto& fmode = g.words ({ { LP, "lp" }, { BP, "bp" }, { HP, "hp" } }, 20, 672);
        fmode.pre = "filter"; fmode.size = 18; fmode.gap = 12; fmode.ci = 1; fmode.isNew = true;
        fmode.getChoice = [dev, sel] { return dev->choice (FMode0 + sel()); };
        fmode.setChoice = [dev, sel] (int v) { dev->setRaw (FMode0 + sel(), (float) v); };

        // Drive: a valve whose filament heats up as you pull it up
        auto vertical = [dev, padParam] (int base, float per) -> std::function<std::optional<Gesture> (float, float)>
        {
            return [dev, padParam, base, per] (float, float ly) -> std::optional<Gesture>
            {
                const float s = dev->get (padParam (base));
                Gesture gs; gs.move = [dev, padParam, base, per, s, ly] (float, float y) { dev->set (padParam (base), s + (ly - y) / per); };
                return gs;
            };
        };
        auto& drv = g.dragArea ({ 306, 486, 88, 170 }, Cursor::UpDownResizeCursor);
        drv.down = vertical (Drive0, 130.0f);
        drv.tip = [dev, padParam] { return "drive   " + F::pct (dev->get (padParam (Drive0))); };
        drv.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (Drive0), 0.0f); };
        // Snap: a lightning bolt that grows with the transient
        auto& snp = g.dragArea ({ 436, 486, 64, 170 }, Cursor::UpDownResizeCursor);
        snp.down = vertical (PSnap0, 120.0f);
        snp.tip = [dev, padParam] { return "snap   " + F::pct (dev->get (padParam (PSnap0))) + "   on top of the kit's"; };
        snp.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (PSnap0), 0.0f); };
        // Pan: a see-saw
        auto& pan = g.dragArea ({ 524, 500, 236, 130 }, Cursor::LeftRightResizeCursor);
        pan.tip = [dev, padParam]
        {
            const int v = juce::roundToInt ((dev->get (padParam (Pan0)) - 0.5f) * 200.0f);
            return "pan   " + (v == 0 ? String ("centre") : (v < 0 ? "left " : "right ") + String (std::abs (v)));
        };
        pan.doubleClick = [dev, padParam] (float, float) { dev->set (padParam (Pan0), 0.5f); };
        pan.down = [dev, padParam] (float lx, float) -> std::optional<Gesture>
        {
            const float s = dev->get (padParam (Pan0));
            Gesture gs; gs.move = [dev, padParam, s, lx] (float x, float) { dev->set (padParam (Pan0), s + (x - lx) / 220.0f); };
            return gs;
        };
        // Choke: three chain links; a pad in a link cuts off the others in it
        auto& chk = g.dragArea ({ 770, 500, 100, 66 }, Cursor::PointingHandCursor);
        chk.tip = [dev, sel]
        {
            const int ch = dev->choice (Choke0 + sel());
            if (ch <= 0) return String ("no choke group");
            int n = 0;
            for (int p = 0; p < 16; ++p) if (dev->choice (Choke0 + p) == ch) ++n;
            return String ("choke group ") + "abc"[juce::jlimit (1, 3, ch) - 1] + "   " + String (n) + " pads";
        };
        chk.down = [dev, sel, w] (float lx, float) -> std::optional<Gesture>
        {
            const int link = 1 + juce::jlimit (0, 2, (int) std::floor ((lx - 775.0f) / 30.0f));
            const int now = dev->choice (Choke0 + sel());
            dev->setRaw (Choke0 + sel(), (float) (now == link ? 0 : link));
            w->say (now == link ? String ("no choke group") : String ("choke group ") + "abc"[link - 1] + "   a pad here cuts the others off");
            return Gesture {};
        };
        auto& out = g.words ({ { 0, "kit" }, { 1, "own" } }, 770, 672);
        out.pre = "out"; out.size = 17; out.gap = 10; out.ci = 2; out.isNew = true;
        out.getChoice = [dev, sel] { return dev->choice (Out0 + sel()); };
        out.setChoice = [dev, sel, w] (int v)
        {
            dev->setRaw (Out0 + sel(), (float) v);
            w->say (v == 1 ? "out of the rack's own jack, past the kit's dirt (falls back to the kit when the jack is loose)" : "into the kit");
        };

        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const int p = sel();
            const auto m = tm();
            const auto pc = colours();
            const int voiceNow = dev->choice (Voice0 + p);
            const bool sample = voiceNow == Sample;
            zone (gr, 894, 696, c, nameNow(), "pad " + String (p + 1) + ", this pad only", 26);
            if (auto* mod = dev->module(); mod != nullptr && ! pc[(size_t) p].isTransparent())
            {
                bulb (gr, 706, 26, pc[(size_t) p], 1.0f, 7);
                text (gr, rangeWord (DrumKit::rangeOf (*mod, p)), 690, 32, 14, pc[(size_t) p], 0.6f, Align::right);
            }
            for (int j = 0; j < 3; ++j)
                voiceFaders[(size_t) j]->text = sample ? String() : String (voiceParam (voiceNow, j));

            // The wave stage: the sound in bare glass, lit where the envelope lets it through
            Lines off, lit;
            const bool rv = dev->on (Reverse0 + p);
            const float snapNow = juce::jmin (1.5f, dev->get (Snap) + dev->get (PSnap0 + p));
            for (int j = 0; j < 72; ++j)
            {
                const float tj = (float) j / 71.0f * m.sec, x = SX + 4 + (float) j * (SW - 8) / 71.0f;
                const float h0 = juce::jmax (1.5f, m.pic.pk[(size_t) juce::jmin (63, (rv ? 71 - j : j) * 64 / 72)] * amp);
                off.push_back ({ { x, mid - h0 }, { x, mid + h0 } });
                if (tj >= m.st)
                {
                    const float u = tj - m.st;
                    const float h1 = juce::jmin (amp + 14, h0 * env (m, u) * (1 + snapNow * 1.5f * std::exp (-u * 120)) / (1 + snapNow * 0.5f));
                    if (h1 > 1) lit.push_back ({ { x, mid - h1 }, { x, mid + h1 } });
                }
            }
            tube (gr, off, c[0], opts (0.75f, true, -1));
            if (! lit.empty()) { TubeOptions lo = opts (0.95f, false, -1); lo.step = 6; tube (gr, lit, c[0], lo); }
            Line ep { { X (m, m.st), mid - 14 } };
            for (int j = 0; j <= 70; ++j) { const float tt = m.st + (m.sec - m.st) * (float) j / 70.0f; ep.push_back ({ X (m, tt), mid - amp * env (m, tt - m.st) - 14 }); }
            tube (gr, ep, c[1], opts (0.9f));
            // Where it is playing now
            if (auto* node = dev->live(); node != nullptr && node->views[(size_t) p].pitch.load() >= 0)
            {
                const float age = node->views[(size_t) p].age.load();
                bulb (gr, X (m, m.st + age), mid - amp * env (m, age) - 14, C::white, 0.9f, 4.5f);
            }
            if (sample)
            {
                const float bx = X (m, m.st);
                tube (gr, Line { { bx + 10, mid - amp - 26 }, { bx, mid - amp - 26 }, { bx, mid + amp + 12 }, { bx + 10, mid + amp + 12 } }, c[2], opts (0.9f));
                text (gr, "start", bx + 12, mid + amp + 28, 14, c[2], 0.45f);
                // The reverse arrow, lit when it plays backwards
                const float ax = 124, ay = 344, dir = rv ? -1.0f : 1.0f, tipX = dir > 0 ? ax + 70 : ax;
                tube (gr, Lines { dir > 0 ? Line { { ax, ay }, { ax + 70, ay } } : Line { { ax + 70, ay }, { ax, ay } },
                                  Line { { tipX - dir * 11, ay - 8 }, { tipX, ay }, { tipX - dir * 11, ay + 8 } } }, c[2], opts (0.9f, ! rv));
            }
            text (gr, "attack", X (m, m.st + m.aT) - 16, mid - amp - 32, 14, c[1], 0.45f);
            text (gr, "decay", juce::jmin (SX + SW - 44, X (m, m.st + m.aT + m.hT + juce::jmin (m.tau > 0 ? m.tau : m.sec, m.sec) * 1.2f) + 12), mid - amp * 0.3f - 28, 14, c[1], 0.45f);
            if (sample)
            {
                const auto file = st->names[(size_t) p].toLowerCase();
                text (gr, file + "   " + String (m.sec, 2) + " s", 20, 402, 20, c[2], 0.75f);
                text (gr, "drop a file on the pad, drag one in from the browser, or pick an analogue voice above", 20, 434, 15, c[2], 0.35f);
            }
            else
            {
                static const char* what[] = { "", "sine core, pitch sweep, click", "two drum heads and filtered noise", "six square oscillators, high passed",
                                              "noise bursts, band pass, room tail", "sine core, pitch sweep, skin noise", "two square oscillators, band pass" };
                const float tw = text (gr, String ("analogue ") + voiceName (voiceNow), 20, 400, 22, c[1], 0.9f);
                text (gr, what[juce::jlimit (0, 6, voiceNow)], 36 + tw, 400, 15, c[2], 0.5f);
            }

            // Level: seventeen lamps, the top three in the second colour
            const int nL = 17;
            const float lvOn = dev->get (Level0 + p) / 0.8f * 0.8f * (float) nL;
            for (int i = 0; i < nL; ++i) bulb (gr, 612, 334 - (float) i * 14, i >= nL - 3 ? c[1] : c[0], clampf (lvOn - (float) i), 6);
            text (gr, "level", 612, 366, 16, c[0], 0.55f, Align::centre);

            // Pitch arc
            const float pv = dev->get (Pitch0 + p);
            Lines ticksOn, ticksOff, octOn, octOff;
            for (int k = 0; k <= 48; ++k)
            {
                const float a = A0 + AS * (float) k / 48.0f, v = (float) k / 48.0f;
                const bool oc = k % 12 == 0, inr = (v - 0.5f) * (pv - 0.5f) >= 0 && std::abs (v - 0.5f) <= std::abs (pv - 0.5f) + 1e-6f;
                const float r0 = PR - (oc ? 24.0f : 13.0f);
                Line tk { { PC.x + std::cos (a) * r0, PC.y + std::sin (a) * r0 }, { PC.x + std::cos (a) * PR, PC.y + std::sin (a) * PR } };
                (oc ? (inr ? octOn : octOff) : (inr ? ticksOn : ticksOff)).push_back (tk);
            }
            tube (gr, ticksOff, c[0], opts (0.5f, true));
            tube (gr, octOff, c[0], opts (0.8f, true));
            if (! ticksOn.empty()) tube (gr, ticksOn, mix (c[0], C::white, 0.35f), opts (0.5f));
            if (! octOn.empty()) tube (gr, octOn, mix (c[0], C::white, 0.35f), opts (0.8f));
            for (int s : { -24, -12, 0, 12, 24 })
            {
                const float a = A0 + AS * (float) (s + 24) / 48.0f;
                label (gr, (s > 0 ? "+" : "") + String (s), PC.x + std::cos (a) * (PR + 16), PC.y + std::sin (a) * (PR + 16) + 4, Align::centre, Colour (0xff5c5e66), 12);
            }
            const float na = A0 + AS * pv;
            const P tipP (PC.x + std::cos (na) * (PR - 32), PC.y + std::sin (na) * (PR - 32));
            tube (gr, Line { PC, tipP }, c[1], opts (1.05f));
            bulb (gr, tipP.x, tipP.y, c[1], 1.0f, 6.5f);
            bulb (gr, PC.x, PC.y, c[2], 0.6f, 5);
            text (gr, "pitch", PC.x, PC.y + 36, 16, c[0], 0.55f, Align::centre);
            text (gr, signedSt (pv, 48.0f), PC.x, PC.y + 58, 15, c[1], 0.6f, Align::centre);

            // Filter curve
            const float fc = cutoffHz (dev->get (Cutoff0 + p)), res = dev->get (Res0 + p);
            const int fm = dev->choice (FMode0 + p);
            Line fcv;
            for (int i = 0; i <= 70; ++i) { const float f = 20.0f * std::pow (1000.0f, (float) i / 70.0f); fcv.push_back ({ Xf (f), Yf (padFilter (f, fc, res, fm)) }); }
            tube (gr, Line { { FX0, FY0 + FH }, { FX0 + FW, FY0 + FH } }, c[0], opts (0.5f, true));
            TubeOptions fo = opts (0.95f); fo.base = FY0 + FH;
            tube (gr, fcv, c[0], fo);

            // Drive valve
            const float vx = 350, heat = dev->get (Drive0 + p);
            TubeOptions vo = opts (0.8f, true); vo.closed = true;
            tube (gr, roundRect (vx - 34, 490, 68, 140, 34), c[1], vo);
            tube (gr, Lines { Line { { vx - 24, 640 }, { vx + 24, 640 } }, Line { { vx - 16, 648 }, { vx + 16, 648 } } }, c[1], opts (0.7f, true));
            if (heat > 0.02f)
            {
                juce::ColourGradient grad (rgba (c[1], 0.38f * heat * look().glow), vx, 560, rgba (c[1], 0.0f), vx + 64, 560, true);
                gr.setGradientFill (grad);
                gr.fillRect (vx - 66, 494.0f, 132.0f, 140.0f);
            }
            Line fil { { vx - 20, 616 } };
            const int zz = 4 + juce::roundToInt (heat * 8);
            const float hz = 14 + heat * 56;
            for (int i = 0; i <= zz; ++i) fil.push_back ({ vx - 20 + 40.0f * (float) i / (float) zz, 616 - hz - (i % 2 ? 12.0f : 0.0f) });
            fil.push_back ({ vx + 20, 616 });
            tube (gr, fil, c[1], opts (0.55f + heat * 0.7f, heat <= 0.02f));
            text (gr, "drive", vx, 672, 17, c[0], 0.55f, Align::centre);

            // Snap bolt
            const float sx = 468, H = 24 + clampf (dev->get (PSnap0 + p)) * 116;
            auto bolt = [sx] (float hh) { return Line { { sx + 7, 640 }, { sx - 10, 640 - hh * 0.45f }, { sx + 9, 640 - hh * 0.5f }, { sx - 7, 640 - hh } }; };
            tube (gr, bolt (140), c[2], opts (0.7f, true));
            tube (gr, bolt (H), c[2], opts (1.0f));
            text (gr, "snap", sx, 672, 17, c[0], 0.55f, Align::centre);

            // Pan see-saw
            const P pvt (642, 580);
            const float pan01 = dev->get (Pan0 + p), ang = (pan01 - 0.5f) * 0.6f, ex = std::cos (ang) * 104, ey = std::sin (ang) * 104;
            TubeOptions tri = opts (0.8f, true); tri.closed = true;
            tube (gr, Line { pvt, { pvt.x - 18, pvt.y + 36 }, { pvt.x + 18, pvt.y + 36 } }, c[2], tri);
            tube (gr, Line { { pvt.x - ex, pvt.y - ey }, { pvt.x + ex, pvt.y + ey } }, c[0], opts (1.1f));
            bulb (gr, pvt.x - ex, pvt.y - ey - 13, c[1], clampf ((1 - pan01) * 1.6f), 8);
            bulb (gr, pvt.x + ex, pvt.y + ey - 13, c[1], clampf (pan01 * 1.6f), 8);
            bulb (gr, pvt.x, pvt.y, c[2], 0.7f, 4.5f);
            text (gr, "l", pvt.x - ex - 4, pvt.y - ey + 22, 15, c[2], 0.45f);
            text (gr, "r", pvt.x + ex - 4, pvt.y + ey + 22, 15, c[2], 0.45f);
            text (gr, "pan", pvt.x, 672, 17, c[0], 0.55f, Align::centre);

            // Choke chain links
            const int ch = dev->choice (Choke0 + p);
            for (int k = 0; k < 3; ++k)
            {
                const float cx = 790.0f + (float) k * 30.0f, cy = 524.0f;
                const bool me = ch == k + 1;
                tube (gr, ellipse (cx, cy, 16, 12), c[1], opts (0.75f, ! me));
                text (gr, String::charToString ((juce::juce_wchar) "abc"[k]), cx, 560, 15, c[1], me ? 0.85f : 0.0f, Align::centre);
            }
            text (gr, "choke", 820, 600, 17, c[0], 0.55f, Align::centre);
        };
    }
}

//==============================================================================
// Drum Rack Sequencer

void drumSeqWall (Builder& b)
{
    using namespace Param::DrumSeq;
    using SeqStep = Param::DrumSeq::Step;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::YL; b.out.pal[1] = C::PK; b.out.pal[2] = C::CY;
    b.out.parts = { C::OR, C::YL, C::CY, C::PK };
    enum { Groove = 1, Matrix, Row };

    auto rack = [dev] { return drumRackFor (*dev); };
    auto padName = [rack] (int i)
    {
        if (auto* m = rack()) return DrumKit::padName (*m, i).toLowerCase();
        return Samples::defaultPads()[(size_t) i].toLowerCase();
    };
    auto colours = [rack] { auto* m = rack(); return m != nullptr ? padColours (*m) : std::array<Colour, 16> {}; };
    auto sel = [dev] { return juce::jlimit (0, rows - 1, dev->choice (Selected)); };
    auto pattern = [dev] { return juce::jlimit (0, patterns - 1, dev->choice (Pattern)); };
    auto len = [dev] (int row) { return juce::jlimit (1, steps, juce::roundToInt (dev->get (Length0 + row))); };
    auto stepAt = [dev, pattern] (int row, int s) { return decode (dev->get (stepParam (pattern(), row, s))); };
    auto setStep = [dev, pattern] (int row, int s, const SeqStep& v) { dev->setRaw (stepParam (pattern(), row, s), encode (v)); };
    auto playingStep = [dev] (int row) -> int
    {
        auto* node = dev->live();
        if (node == nullptr || ! dev->ctx.engine.isPlaying()) return -1;
        return node->views[(size_t) row].slice.load();
    };
    auto audition = [dev] (int row, float vel) { dev->note (36 + row, true, vel); dev->note (36 + row, false); };
    auto SXs = [] (int s) { return 232.0f + (float) s * 72.0f + (float) (s / 4) * 14.0f; };
    auto nearStep = [] (const std::function<float (int)>& fx, float lx, float width)
    {
        int best = -1;
        float bd = width;
        for (int s = 0; s < steps; ++s) { const float d = std::abs (fx (s) - lx); if (d < bd) { bd = d; best = s; } }
        return best;
    };

    // Title
    {
        auto& g = wall.add (0, 48, 22, 0, 230, 80, 330);
        g.draw = [w] (juce::Graphics& gr, double, float, const Colour* c)
        {
            {
                // The name a size down, so it fits beside the groove
                auto& gl = glow();
                const auto saved = gl.current;
                const auto tr = juce::AffineTransform::scale (0.56f);
                gl.current = tr.followedBy (saved);
                juce::Graphics::ScopedSaveState s (gr);
                gr.addTransform (tr);
                bigTag (gr, "Drum Rack Sequencer", 0, 150, c[0]);
                gl.current = saved;
            }
            text (gr, "its own device, it plays the drum rack it is patched to", 6, 124, 17, c[1], 0.8f);
            const double age = now() - w->readoutTime;
            if (age < 3.5) text (gr, w->readout, 6, 160, 19, C::white, clampf (1.2f - (float) age / 3.0f) * 0.65f);
        };
    }

    // The groove: what changes the whole sequencer
    {
        auto& g = wall.add (Groove, 660, 22, 0, 450, 80, 500);
        auto* eng = &dev->ctx.engine;
        auto& play = g.toggle ("play", 820, 34, [eng] { return eng->isPlaying(); }, [eng] (bool v) { if (v) eng->play(); else eng->stop(); });
        play.size = 26;
        play.tip = [eng] { return String (eng->isPlaying() ? "the song is playing" : "play the song"); };
        auto& on = b.toggle (g, Play, "on", 740, 34, true);
        on.size = 22; on.ci = 1;
        on.tip = [dev] { return String (dev->on (Play) ? "sending its steps to the rack" : "switched off: the rack only hears the track's notes"); };
        auto& pat = b.choice (g, Pattern, { "a", "b", "c", "d" }, 20, 84, true);
        pat.pre = "pattern"; pat.size = 19;
        pat.setChoice = [dev, w] (int v) { dev->setRaw (Pattern, (float) v); w->say (String ("pattern ") + "abcd"[juce::jlimit (0, 3, v)]); };
        auto& rate = b.choice (g, Rate, { "1/8", "1/16", "1/16t", "1/32" }, 290, 84, true);
        rate.pre = "step size"; rate.size = 19;
        auto& copy = g.toggle ("copy to next pattern", 630, 84, [] { return true; }, [dev, w] (bool)
        {
            const int from = juce::jlimit (0, patterns - 1, dev->choice (Pattern)), to = (from + 1) % patterns;
            for (int r = 0; r < rows; ++r)
                for (int s = 0; s < steps; ++s)
                    dev->setRaw (stepParam (to, r, s), dev->get (stepParam (from, r, s)));
            dev->setRaw (Pattern, (float) to);
            w->say (String ("pattern ") + "abcd"[from] + " copied to " + "abcd"[to]);
        });
        copy.size = 16; copy.ci = 1; copy.isNew = true;
        b.fader (g, Swing, "swing", [] (float v) { return String (juce::roundToInt (50 + v * 25)) + "%"; }, 24, 140, 224, 140, P (124, 131));
        b.fader (g, Humanise, "humanise", [] (float v) { return F::pct (v); }, 264, 140, 464, 140, P (364, 149), true);
        g.draw = [dev, rack] (juce::Graphics& gr, double, float, const Colour* c)
        {
            zone (gr, 900, 166, c, "groove", "the whole sequencer");
            text (gr, String (juce::roundToInt (dev->ctx.project.bpm.load())) + " bpm, follows the song", 520, 146, 15, c[2], 0.45f);
            text (gr, rack() != nullptr ? "notes out to the drum rack" : "notes out: patch it to a drum rack", 520, 118, 15, c[1], 0.6f);
        };
    }

    // All sixteen rows
    {
        auto& g = wall.add (Matrix, 40, 196, 0, 760, 210, 700);
        const float RH = 21, top = 62;
        auto rowY = [rack, RH, top] (int i) { const auto o = rowOrder (rack()); return top + (float) o.posOf[(size_t) i] * RH + (float) o.groupOf[(size_t) i] * 12.0f; };
        auto rowAt = [rowY, RH] (float ly) { for (int i = 0; i < rows; ++i) if (std::abs (ly - rowY (i)) <= RH * 0.5f) return i; return -1; };
        auto& a = g.dragArea ({ 14, top - 12, 1490, 16 * RH + 36 }, Cursor::PointingHandCursor);
        a.tip = [sel, padName, len] { return "steps   " + padName (sel()) + "   loops every " + String (len (sel())); };
        a.down = [=] (float lx, float ly) -> std::optional<Gesture>
        {
            const int i = rowAt (ly);
            if (i < 0) return Gesture {};
            if (lx < 200) { dev->setRaw (Selected, (float) i); audition (i, 0.8f); w->say (padName (i)); return Gesture {}; }
            const float bx = SXs (len (i) - 1) + 34;
            if (std::abs (lx - bx) < 12)
            {
                dev->setRaw (Selected, (float) i);
                Gesture gs;
                gs.move = [=] (float x, float)
                {
                    int n = steps;
                    for (int s = 0; s < steps; ++s) if (x < SXs (s) + 34) { n = s + 1; break; }
                    dev->setRaw (Length0 + i, (float) juce::jlimit (1, steps, n));
                    w->say (padName (i) + "   loops every " + String (juce::jlimit (1, steps, n)) + " steps");
                };
                return gs;
            }
            const int s = nearStep (SXs, lx, 34);
            if (s < 0 || s >= len (i)) return Gesture {};
            dev->setRaw (Selected, (float) i);
            auto stp = stepAt (i, s);
            const bool turnOn = ! stp.on;
            stp.on = turnOn;
            setStep (i, s, stp);
            if (turnOn && ! dev->ctx.engine.isPlaying()) audition (i, (float) stp.velocity / 127.0f);
            Gesture gs;
            gs.move = [=] (float x, float)
            {
                const int s2 = nearStep (SXs, x, 34);
                if (s2 < 0 || s2 >= len (i)) return;
                auto st2 = stepAt (i, s2);
                if (st2.on != turnOn) { st2.on = turnOn; setStep (i, s2, st2); }
            };
            return gs;
        };
        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            zone (gr, 1520, 420, c, "steps", "every pad, one row each, drag a ] to change how long a row loops, rows grouped low, mid, high");
            const auto pc = colours();
            const auto* rk = rack();
            for (int s = 0; s < steps; s += 4) label (gr, String (s + 1), SXs (s), 48, Align::centre, Colour (0xff55575e), 12);
            const int chosen = sel();
            for (int i = 0; i < rows; ++i)
            {
                const float y = rowY (i);
                const bool me = i == chosen;
                bool dead = false;
                if (rk != nullptr)
                {
                    bool solo = false;
                    for (int p = 0; p < 16; ++p) solo = solo || rk->knobValues[(size_t) (Param::Drums::Solo0 + p)] > 0.5f;
                    dead = rk->knobValues[(size_t) (Param::Drums::Mute0 + i)] > 0.5f || (solo && rk->knobValues[(size_t) (Param::Drums::Solo0 + i)] <= 0.5f);
                }
                const bool coloured = ! pc[(size_t) i].isTransparent();
                const auto col = coloured ? pc[(size_t) i] : c[me ? 1 : 0];
                if (me) { gr.setColour (rgba (col, 0.1f)); gr.fillRoundedRectangle (16, y - RH * 0.5f + 1, SXs (15) + 70 - 16, RH - 2, 6); }
                bulb (gr, 26, y, col, dead ? 0.15f : 0.85f, 4);
                text (gr, padName (i), 40, y + 5.5f, 16, col, dead ? 0.0f : me ? 0.95f : 0.6f);
                const int L = len (i), ph = playingStep (i);
                for (int s = 0; s < steps; ++s)
                {
                    const float x = SXs (s);
                    if (s >= L) { gr.setColour (Colour (0xff26272b)); gr.fillEllipse (x - 2.4f, y - 2.4f, 4.8f, 4.8f); continue; }
                    const auto stp = stepAt (i, s);
                    const bool here = ph == s;
                    const float v = (float) stp.velocity / 127.0f, chn = (float) stp.chance / 100.0f;
                    const float lv = stp.on ? (0.3f + 0.7f * v) * (chn < 1 ? 0.55f + 0.45f * chn : 1.0f) * (dead ? 0.4f : 1.0f) : here ? 0.4f : 0.0f;
                    const float r = me ? 7.5f : 6.5f;
                    bulb (gr, x, y, here ? C::white : coloured ? (s % 4 ? col : mix (col, C::white, 0.3f)) : c[s % 4 ? 0 : 1], lv, r);
                    if (stp.on && stp.accent()) { gr.setColour (rgba (coloured ? col : c[1], 0.8f)); gr.drawEllipse (x - r - 3.5f, y - r - 3.5f, (r + 3.5f) * 2, (r + 3.5f) * 2, 1.2f); }
                    if (stp.on && stp.chance < 100)
                    {
                        juce::Path arcP;
                        arcP.addCentredArc (x, y, r + 4.5f, r + 4.5f, 0, 0, chn * 2 * pi, true);
                        juce::Path dashed;
                        const float dl[] = { 2.0f, 2.0f };
                        juce::PathStrokeType (1.4f).createDashedStroke (dashed, arcP, dl, 2);
                        gr.setColour (rgba (c[2], 0.85f));
                        gr.fillPath (dashed);
                    }
                    if (stp.on && stp.ratchet > 1)
                    {
                        gr.setColour (mix (coloured ? col : c[2], C::white, 0.4f));
                        for (int k = 0; k < stp.ratchet; ++k) gr.fillEllipse (x + r + 6 + (float) k * 4.5f - 1.7f, y + r - 2 - 1.7f, 3.4f, 3.4f);
                    }
                }
                const float bx = SXs (L - 1) + 34;
                tube (gr, Line { { bx - 7, y - 7 }, { bx, y - 7 }, { bx, y + 7 }, { bx - 7, y + 7 } }, coloured ? col : c[2], opts (me ? 0.85f : 0.6f));
                if (L < steps || me) text (gr, String (L), bx + 10, y + 5, 14, coloured ? col : c[2], me ? 0.75f : 0.4f);
            }
        };
    }

    // The chosen row, opened up: velocity, chance and ratchets for every step
    {
        auto& g = wall.add (Row, 40, 630, 0, 760, 140, 700);
        struct Lane { float y0, h; };
        const Lane vel { 92, 80 }, chance { 190, 44 }, rat { 246, 30 };
        auto edit = [=] (int lane, float lx, float ly)
        {
            const int row = sel(), s = nearStep (SXs, lx, 34);
            if (s < 0 || s >= len (row)) return;
            auto stp = stepAt (row, s);
            const auto& L = lane == 0 ? vel : lane == 1 ? chance : rat;
            const float v = clampf ((L.y0 + L.h - ly) / L.h);
            stp.on = true;
            if (lane == 0)
            {
                stp.velocity = juce::jlimit (6, 127, juce::roundToInt (v * 127.0f));
                w->say ("step " + String (s + 1) + "   velocity " + String (stp.velocity) + (stp.accent() ? "   accent" : ""));
            }
            else if (lane == 1)
            {
                stp.chance = juce::jlimit (5, 100, juce::roundToInt (v * 20.0f) * 5);
                w->say ("step " + String (s + 1) + "   plays " + String (stp.chance) + "% of the time");
            }
            else
            {
                stp.ratchet = 1 + juce::roundToInt (v * 3.0f);
                w->say ("step " + String (s + 1) + "   " + (stp.ratchet > 1 ? String (stp.ratchet) + " hits in one step" : String ("one hit")));
            }
            setStep (row, s, stp);
        };
        const char* laneNames[] = { "velocity", "chance", "ratchets" };
        for (int lane = 0; lane < 3; ++lane)
        {
            const auto& L = lane == 0 ? vel : lane == 1 ? chance : rat;
            auto& a = g.dragArea ({ 200, L.y0 - 6, 1310, L.h + 10 }, Cursor::UpDownResizeCursor);
            a.isNew = true;
            const String nm (laneNames[lane]);
            a.tip = [padName, sel, nm] { return padName (sel()) + "   drag to draw " + nm; };
            a.down = [edit, lane] (float lx, float ly) -> std::optional<Gesture>
            {
                edit (lane, lx, ly);
                Gesture gs; gs.move = [edit, lane] (float x, float y) { edit (lane, x, y); };
                return gs;
            };
        }
        auto& onRow = g.dragArea ({ 200, 52, 1310, 28 }, Cursor::PointingHandCursor);
        onRow.tip = [padName, sel] { return "steps   " + padName (sel()); };
        onRow.down = [=] (float lx, float) -> std::optional<Gesture>
        {
            const int row = sel(), s = nearStep (SXs, lx, 34);
            if (s < 0 || s >= len (row)) return Gesture {};
            auto stp = stepAt (row, s);
            const bool turnOn = ! stp.on;
            stp.on = turnOn;
            setStep (row, s, stp);
            Gesture gs;
            gs.move = [=] (float x, float)
            {
                const int s2 = nearStep (SXs, x, 34);
                if (s2 < 0 || s2 >= len (row)) return;
                auto st2 = stepAt (row, s2);
                if (st2.on != turnOn) { st2.on = turnOn; setStep (row, s2, st2); }
            };
            return gs;
        };
        auto& clear = g.toggle ("clear row", 1390, 32, [] { return true; }, [=] (bool)
        {
            const int row = sel();
            for (int s = 0; s < steps; ++s) setStep (row, s, SeqStep {});
            w->say ("cleared " + padName (row));
        });
        clear.size = 17; clear.ci = 1;
        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const int row = sel();
            const auto pc = colours();
            const bool coloured = ! pc[(size_t) row].isTransparent();
            zone (gr, 1520, 280, c, padName (row), "the chosen row, opened up");
            if (coloured) bulb (gr, 1370, 26, pc[(size_t) row], 1.0f, 6);
            text (gr, "on", 40, 72, 15, c[2], 0.55f);
            text (gr, "velocity", 40, vel.y0 + vel.h - 4, 15, c[2], 0.6f);
            text (gr, "chance", 40, chance.y0 + chance.h - 4, 15, c[2], 0.6f);
            text (gr, "ratchet", 40, rat.y0 + rat.h - 2, 15, c[2], 0.6f);
            const auto col = coloured ? pc[(size_t) row] : c[0];
            const int L = len (row), ph = playingStep (row);
            Lines vl, vo, cl, co;
            for (int s = 0; s < steps; ++s)
            {
                const float x = SXs (s);
                if (s >= L) { gr.setColour (Colour (0xff26272b)); gr.fillEllipse (x - 2.4f, 66 - 2.4f, 4.8f, 4.8f); continue; }
                const auto stp = stepAt (row, s);
                const bool here = ph == s;
                const float v = (float) stp.velocity / 127.0f, chn = (float) stp.chance / 100.0f;
                bulb (gr, x, 66, here ? C::white : s % 4 ? col : mix (col, C::white, 0.3f), stp.on ? 0.3f + 0.7f * v : here ? 0.4f : 0.0f, 8);
                const float vb = vel.y0 + vel.h, cb = chance.y0 + chance.h;
                vo.push_back ({ { x, vb }, { x, vel.y0 } });
                co.push_back ({ { x, cb }, { x, chance.y0 } });
                if (stp.on)
                {
                    vl.push_back ({ { x, vb }, { x, vb - juce::jmax (2.0f, v * vel.h) } });
                    cl.push_back ({ { x, cb }, { x, cb - juce::jmax (2.0f, chn * chance.h) } });
                    if (stp.accent()) text (gr, "accent", x, vel.y0 - 6, 11, col, 0.6f, Align::centre);
                }
                for (int k = 0; k < 4; ++k) bulb (gr, x, rat.y0 + rat.h - 4 - (float) k * 8, col, stp.on && k < stp.ratchet ? 0.9f : 0.0f, 3);
            }
            tube (gr, vo, col, opts (0.55f, true));
            tube (gr, co, c[2], opts (0.55f, true));
            if (! vl.empty()) tube (gr, vl, col, opts (0.9f));
            if (! cl.empty()) tube (gr, cl, c[2], opts (0.8f));
        };
    }
}
}
