#include "PluginHost.h"

juce::File AppData::dir()
{
   #if JUCE_MAC
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/GLOWTAG");
   #else
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GLOWTAG");
   #endif
    d.createDirectory();
    return d;
}

//==============================================================================
class PluginHost::Scanner : public juce::Thread
{
public:
    Scanner (PluginHost& h, std::function<void()> d) : juce::Thread ("Plugin scan"), host (h), done (std::move (d)) {}
    ~Scanner() override { stopThread (10000); }

    void run() override
    {
        const auto deadMansPedal = AppData::dir().getChildFile ("plugin-scan-in-progress.txt");
        const int numFormats = host.formats.getNumFormats();
        for (int f = 0; f < numFormats && ! threadShouldExit(); ++f)
        {
            auto* format = host.formats.getFormat (f);
            juce::PluginDirectoryScanner scanner (host.known, *format, format->getDefaultLocationsToSearch(), true, deadMansPedal, true);
            juce::String name;
            while (! threadShouldExit())
            {
                {
                    const juce::ScopedLock sl (host.nameLock);
                    host.currentName = scanner.getNextPluginFileThatWillBeScanned();
                }
                host.progress = ((float) f + scanner.getProgress()) / (float) numFormats;
                if (! scanner.scanNextFile (true, name))
                    break;
            }
        }
        host.progress = 1.0f;
        host.scanning = false;
        juce::MessageManager::callAsync ([host = &host, done = done]
        {
            host->save();
            if (done) done();
        });
    }

private:
    PluginHost& host;
    std::function<void()> done;
};

//==============================================================================
PluginHost& PluginHost::get()
{
    static PluginHost host;
    return host;
}

PluginHost::PluginHost()
{
    formats.addDefaultFormats();
    load();
}

juce::File PluginHost::listFile() const
{
    return AppData::dir().getChildFile ("plugins.xml");
}

void PluginHost::load()
{
    if (auto xml = juce::XmlDocument::parse (listFile()))
        known.recreateFromXml (*xml);
}

void PluginHost::save()
{
    if (auto xml = known.createXml())
        xml->writeTo (listFile());
}

juce::Array<juce::PluginDescription> PluginHost::list() const
{
    auto types = known.getTypes();
    std::sort (types.begin(), types.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return types;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const juce::String& id, double sampleRate, int blockSize, juce::String& error)
{
    if (id.isEmpty())
    {
        error = "No plugin chosen";
        return nullptr;
    }
    auto desc = known.getTypeForIdentifierString (id);
    if (desc == nullptr)
    {
        error = "Plugin not found: " + id;
        return nullptr;
    }
    return formats.createPluginInstance (*desc, sampleRate, blockSize, error);
}

void PluginHost::scan (std::function<void()> done)
{
    if (scanning.load())
        return;
    scanner.reset();
    scanning = true;
    progress = 0.0f;
    scanner = std::make_unique<Scanner> (*this, std::move (done));
    scanner->startThread();
}

juce::String PluginHost::scanningName() const
{
    const juce::ScopedLock sl (nameLock);
    return currentName;
}
