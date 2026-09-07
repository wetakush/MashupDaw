#include "PluginHost.h"
#include "Core/Log.h"

namespace mashup
{
class PluginHost::ScanThread : public juce::Thread
{
public:
    ScanThread (PluginHost& h, bool rescan) : Thread ("VST3 scan"), host (h), rescanExisting (rescan) {}
    void run() override
    {
        for (auto* format : host.formatManager.getFormats())
        {
            juce::PluginDirectoryScanner scanner (host.knownPlugins, *format, host.getSearchPath(), true,
                                                  juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MashupDaw").getChildFile ("deadplugins.txt"),
                                                  ! rescanExisting);
            juce::String name;
            while (! threadShouldExit())
            {
                { const juce::ScopedLock l (host.nameLock); host.currentName = scanner.getNextPluginFileThatWillBeScanned(); }
                const bool more = scanner.scanNextFile (true, name);
                host.scanProgress.store (scanner.getProgress());
                if (! more) break;
            }
        }
        host.scanning.store (false);
        juce::MessageManager::callAsync ([h = &host] { h->saveList(); h->sendChangeMessage(); });
    }
private:
    PluginHost& host;
    bool rescanExisting;
};

PluginHost::PluginHost (juce::ApplicationProperties& p) : props (p)
{
    formatManager.addFormat (new juce::VST3PluginFormat());
    if (auto* settings = props.getUserSettings())
        if (auto xml = settings->getXmlValue ("knownPlugins"))
            knownPlugins.recreateFromXml (*xml);
}

PluginHost::~PluginHost() { if (scanThread) scanThread->stopThread (5000); }

juce::FileSearchPath PluginHost::getSearchPath() const
{
    juce::FileSearchPath path;
    if (auto* settings = props.getUserSettings())
    {
        const auto s = settings->getValue ("vst3SearchPath");
        if (s.isNotEmpty()) path = juce::FileSearchPath (s);
    }
    if (path.getNumPaths() == 0)
        for (auto* f : formatManager.getFormats()) path.addPath (f->getDefaultLocationsToSearch());
    return path;
}

void PluginHost::setSearchPath (const juce::FileSearchPath& p)
{
    if (auto* settings = props.getUserSettings()) { settings->setValue ("vst3SearchPath", p.toString()); settings->saveIfNeeded(); }
}

void PluginHost::scanAsync (bool rescan)
{
    if (scanning.exchange (true)) return;
    scanProgress.store (0.0f);
    if (scanThread) scanThread->stopThread (5000);
    scanThread = std::make_unique<ScanThread> (*this, rescan);
    scanThread->startThread();
}

void PluginHost::saveList()
{
    if (auto* settings = props.getUserSettings())
        if (auto xml = knownPlugins.createXml()) { settings->setValue ("knownPlugins", xml.get()); settings->saveIfNeeded(); }
}

std::unique_ptr<juce::AudioProcessor> PluginHost::createInstance (const juce::String& pluginId, double sampleRate, int blockSize)
{
    for (const auto& d : knownPlugins.getTypes())
        if (d.createIdentifierString() == pluginId || d.fileOrIdentifier == pluginId)
        {
            juce::String error;
            auto inst = formatManager.createPluginInstance (d, sampleRate, blockSize, error);
            if (! inst) log ("Plugin instantiation failed: " + error);
            return inst;
        }
    log ("Unknown plugin id: " + pluginId);
    return nullptr;
}
} // namespace mashup
