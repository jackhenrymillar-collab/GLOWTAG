#include "DrumKit.h"
#include "DeviceParams.h"
#include "Samples.h"

namespace DrumKit
{
namespace
{
    using namespace Param::Drums;

    // Every stored control a kit carries: the kit's own sound and each pad's
    std::vector<int> kitParams()
    {
        std::vector<int> v { Tune, Decay, Snap, Gain, Dirt, VelSens };
        for (int base : { (int) Level0, (int) Pitch0, (int) Pan0, (int) Choke0 })
            for (int p = 0; p < 16; ++p) v.push_back (base + p);
        for (int i = Attack0; i < Count; ++i) v.push_back (i);
        return v;
    }

    const char* builtInNames[] = { "factory", "analogue" };
    constexpr int numBuiltIn = 2;

    void builtIn (Module& rack, int which)
    {
        const auto& info = moduleInfo (ModuleType::Drums);
        for (int i : kitParams())
            rack.knobValues[(size_t) i] = info.defaultValue (i);
        for (auto& n : rack.padNames) n = {};
        if (which == 1)
        {
            // Every pad an analogue voice, in the same places as the factory kit's samples
            const int voices[16] = { Kick, Perc, Snare, Clap, Kick, Tom, Hat, Tom, Hat, Tom, Hat, Perc, Perc, Hat, Kick, Hat };
            const float a[16] = { 0.35f, 0.7f, 0.4f, 0.45f, 0.6f, 0.25f, 0.5f, 0.6f, 0.9f, 0.45f, 0.5f, 0.65f, 0.4f, 0.2f, 0.05f, 0.75f };
            const float b[16] = { 0.55f, 0.3f, 0.6f, 0.45f, 0.4f, 0.45f, 0.35f, 0.45f, 0.9f, 0.2f, 0.35f, 0.2f, 0.45f, 0.8f, 0.8f, 0.5f };
            const float c[16] = { 0.35f, 0.15f, 0.45f, 0.35f, 0.6f, 0.25f, 0.85f, 0.25f, 0.95f, 0.1f, 0.2f, 0.1f, 0.45f, 0.0f, 0.0f, 0.1f };
            for (int p = 0; p < 16; ++p)
            {
                rack.knobValues[(size_t) (Voice0 + p)] = (float) voices[p];
                rack.knobValues[(size_t) (VoiceA0 + p)] = a[p];
                rack.knobValues[(size_t) (VoiceB0 + p)] = b[p];
                rack.knobValues[(size_t) (VoiceC0 + p)] = c[p];
            }
        }
        rack.kitName = builtInNames[juce::jlimit (0, numBuiltIn - 1, which)];
    }

    bool isAudio (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"); }
}

Range rangeOfName (const juce::String& sampleName)
{
    const auto n = sampleName.toLowerCase();
    for (auto* w : { "kick", "sub", "tom", "808", "bass", "boom", "conga low", "deep" })
        if (n.contains (w)) return Range::Low;
    for (auto* w : { "hat", "shaker", "crash", "ride", "cymbal", "tamb", "shk", "hh", "splash" })
        if (n.contains (w)) return Range::High;
    return Range::Mid;
}

juce::String padName (const Module& rack, int pad)
{
    pad = juce::jlimit (0, 15, pad);
    const int v = juce::roundToInt (rack.knobValues.size() > (size_t) (Voice0 + pad) ? rack.knobValues[(size_t) (Voice0 + pad)] : 0.0f);
    if (v > Sample && v < numVoices) return juce::String ("analogue ") + voiceName (v);
    const auto& n = rack.padNames[(size_t) pad].isNotEmpty() ? rack.padNames[(size_t) pad] : Samples::defaultPads()[(size_t) pad];
    return juce::File::isAbsolutePath (n) ? juce::File (n).getFileNameWithoutExtension() : n;
}

Range rangeOf (const Module& rack, int pad)
{
    pad = juce::jlimit (0, 15, pad);
    const int v = rack.knobValues.size() > (size_t) (Voice0 + pad) ? juce::roundToInt (rack.knobValues[(size_t) (Voice0 + pad)]) : 0;
    if (v == Kick || v == Tom) return Range::Low;
    if (v == Hat) return Range::High;
    if (v > Sample && v < numVoices) return Range::Mid;
    return rangeOfName (padName (rack, pad));
}

juce::var toVar (const Module& rack)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("glowkit", 1);
    juce::Array<juce::var> pads, values, which;
    for (auto& n : rack.padNames) pads.add (n);
    for (int i : kitParams())
        if ((size_t) i < rack.knobValues.size()) { which.add (i); values.add (rack.knobValues[(size_t) i]); }
    o->setProperty ("pads", pads);
    o->setProperty ("params", which);
    o->setProperty ("values", values);
    return juce::var (o);
}

void apply (Module& rack, const juce::var& kit)
{
    if (rack.knobValues.size() < (size_t) Count) return;
    const auto& info = moduleInfo (ModuleType::Drums);
    for (int i : kitParams()) rack.knobValues[(size_t) i] = info.defaultValue (i);
    if (auto* pads = kit["pads"].getArray())
        for (int i = 0; i < 16; ++i)
            rack.padNames[(size_t) i] = i < pads->size() ? (*pads)[i].toString() : juce::String();
    auto* which = kit["params"].getArray();
    auto* values = kit["values"].getArray();
    if (which != nullptr && values != nullptr)
        for (int k = 0; k < which->size() && k < values->size(); ++k)
        {
            const int i = (int) (*which)[k];
            if (i >= 0 && i < Count && i != Selected && i != Swing && (i < Steps0 || i >= Steps0 + 16))
                rack.knobValues[(size_t) i] = (float) (*values)[k];
        }
}

juce::File folder()
{
   #if JUCE_MAC
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/GLOWTAG/Kits");
   #else
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GLOWTAG/Kits");
   #endif
    return d;
}

juce::String fileExtension() { return ".glowkit"; }

std::vector<Entry> list()
{
    std::vector<Entry> out;
    for (int i = 0; i < numBuiltIn; ++i) out.push_back ({ builtInNames[i], {}, i });
    auto files = folder().findChildFiles (juce::File::findFiles, false, "*" + fileExtension());
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    for (auto& f : files) out.push_back ({ f.getFileNameWithoutExtension(), f, -1 });
    return out;
}

int indexOf (const std::vector<Entry>& kits, const juce::String& name)
{
    for (size_t i = 0; i < kits.size(); ++i)
        if (kits[i].name == name) return (int) i;
    return -1;
}

bool load (Module& rack, const Entry& e)
{
    if (rack.knobValues.size() < (size_t) Count) return false;
    if (e.builtIn >= 0) { builtIn (rack, e.builtIn); return true; }
    const auto v = juce::JSON::parse (e.file);
    if (! v.isObject() || ! v.hasProperty ("glowkit")) return false;
    apply (rack, v);
    rack.kitName = e.name;
    return true;
}

juce::String nextFreeName (const juce::String& base)
{
    const auto kits = list();
    for (int n = 1; n < 1000; ++n)
    {
        const auto name = base + " " + juce::String (n).paddedLeft ('0', 2);
        if (indexOf (kits, name) < 0) return name;
    }
    return base;
}

juce::File save (const Module& rack, const juce::String& name)
{
    const auto safeName = juce::File::createLegalFileName (name.trim());
    if (safeName.isEmpty()) return {};
    auto f = folder().getChildFile (safeName + fileExtension());
    if (! folder().createDirectory()) return {};
    if (! f.replaceWithText (juce::JSON::toString (toVar (rack)))) return {};
    return f;
}

int fillFromFolder (Module& rack, const juce::File& dir)
{
    if (! dir.isDirectory() || rack.knobValues.size() < (size_t) Count) return 0;
    auto files = dir.findChildFiles (juce::File::findFiles, false);
    if (std::none_of (files.begin(), files.end(), isAudio))
        files = dir.findChildFiles (juce::File::findFiles, true);    // a folder of folders: look inside
    files.removeIf ([] (const juce::File& f) { return ! isAudio (f) || f.getFileName().startsWithChar ('.'); });
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    std::vector<juce::File> byRange[3];
    for (auto& f : files) byRange[(int) rangeOfName (f.getFileNameWithoutExtension())].push_back (f);
    std::vector<juce::File> order;
    for (auto& r : byRange) for (auto& f : r) order.push_back (f);
    const int n = juce::jmin (16, (int) order.size());
    for (int p = 0; p < n; ++p)
    {
        rack.padNames[(size_t) p] = order[(size_t) p].getFullPathName();
        rack.knobValues[(size_t) (Voice0 + p)] = (float) Sample;
        rack.knobValues[(size_t) (Start0 + p)] = 0.0f;
        rack.knobValues[(size_t) (Reverse0 + p)] = 0.0f;
    }
    if (n > 0) rack.kitName = dir.getFileName();
    return n;
}
}
