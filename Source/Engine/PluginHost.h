#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>

// Where GLOWTAG keeps its own files: the plugin list, settings, the last session
namespace AppData
{
    juce::File dir();
}

// Hosting for AU (Mac) and VST3 plugins: the list of plugins found on this machine and the factory
// the Plugin device uses to make an instance. Scanning runs on a background thread; instances are
// created on the message thread when a patch is rebuilt.
class PluginHost
{
public:
    static PluginHost& get();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

    // Plugins sorted by name, for the browser
    juce::Array<juce::PluginDescription> list() const;

    // Make an instance from a PluginDescription::createIdentifierString(); null (with `error` set) if it can't
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::String& id, double sampleRate, int blockSize, juce::String& error);

    // Look through the standard plugin folders. `done` is called on the message thread.
    void scan (std::function<void()> done);
    bool isScanning() const { return scanning.load(); }
    float scanProgress() const { return progress.load(); }
    juce::String scanningName() const;
    void load();
    void save();

private:
    PluginHost();
    class Scanner;
    std::unique_ptr<Scanner> scanner;
    std::atomic<bool> scanning { false };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection nameLock;
    juce::String currentName;
    juce::File listFile() const;
    friend class Scanner;
};
