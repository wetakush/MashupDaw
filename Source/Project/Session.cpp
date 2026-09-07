#include "Session.h"
#include "Import/SourceLibrary.h"
#include "Effects/EffectFactory.h"
#include "Plugins/PluginHost.h"
#include "AudioEngine/AudioEngine.h"
#include "Waveform/WaveformCacheManager.h"
#include "Analysis/AnalysisService.h"
#include "Commands/KeyMap.h"
#include "Project/ProjectController.h"
#include "Recording/RecordingController.h"
#include "StemSeparation/StemSeparationService.h"
#include "StemSeparation/StemPlacer.h"
#include "Automation/AutomationRecorder.h"
#include "Core/Log.h"

namespace mashup
{
Session::Session()
    : project (undoManager),
      threadPool (juce::ThreadPool::Options().withNumberOfThreads (juce::jmax (2, juce::SystemStats::getNumCpus() - 1)))
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "MashupDaw";
    opts.filenameSuffix = "settings";
    opts.folderName = "MashupDaw";
    opts.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (opts);

    sourceLibrary = std::make_unique<SourceLibrary> (project, threadPool);
    pluginHost = std::make_unique<PluginHost> (appProperties);
    effectFactory = std::make_unique<EffectFactory>();
    effectFactory->setPluginHost (pluginHost.get());
    waveformCache = std::make_unique<WaveformCacheManager> (*sourceLibrary, threadPool, [this] { return getCacheDirectory(); });
    analysis = std::make_unique<AnalysisService> (project, *sourceLibrary, threadPool);
    audioEngine = std::make_unique<AudioEngine> (project, *sourceLibrary, *effectFactory, appProperties);
    const auto err = audioEngine->initialiseDevice();
    if (err.isNotEmpty()) log ("Audio: " + err);
    project.setSampleRate (audioEngine->getSampleRate());
    keyMap = std::make_unique<KeyMap> (appProperties);
    projectController = std::make_unique<ProjectController> (*this);
    recordingController = std::make_unique<RecordingController> (*this);
    stemSeparation = std::make_unique<StemSeparationService> (project, *sourceLibrary, [this] { return getCacheDirectory(); });
    stemPlacer = std::make_unique<StemPlacer> (*this);
    automationRecorder = std::make_unique<AutomationRecorder> (*this);
}

Session::~Session()
{
    automationRecorder.reset();
    stemPlacer.reset();
    stemSeparation.reset();
    recordingController.reset();
    projectController.reset();
    keyMap.reset();
    audioEngine.reset();
    threadPool.removeAllJobs (true, 4000);
    analysis.reset();
    waveformCache.reset();
    sourceLibrary.reset();
}

juce::File Session::getCacheDirectory() const
{
    if (projectFile != juce::File())
        return projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + ".mashupcache");
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MashupDaw").getChildFile ("cache");
}
} // namespace mashup
