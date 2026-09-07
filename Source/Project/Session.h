#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "Project/ProjectModel.h"

namespace mashup
{
class AudioEngine;
class SourceLibrary;
class EffectFactory;
class PluginHost;
class WaveformCacheManager;
class AnalysisService;
class KeyMap;
class ProjectController;
class RecordingController;
class StemSeparationService;
class StemPlacer;
class AutomationRecorder;

/** Owns every long-lived service of the running application. Created once by the JUCEApplication.
    Services are added phase by phase (see docs/ARCHITECTURE.md). */
class Session
{
public:
    Session();
    ~Session();

    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    ProjectModel& getProject() noexcept { return project; }
    juce::ThreadPool& getThreadPool() noexcept { return threadPool; }
    juce::ApplicationProperties& getAppProperties() noexcept { return appProperties; }
    SourceLibrary& getSourceLibrary() noexcept { return *sourceLibrary; }
    EffectFactory& getEffectFactory() noexcept { return *effectFactory; }
    PluginHost& getPluginHost() noexcept { return *pluginHost; }
    AudioEngine& getAudioEngine() noexcept { return *audioEngine; }
    WaveformCacheManager& getWaveformCache() noexcept { return *waveformCache; }
    AnalysisService& getAnalysis() noexcept { return *analysis; }
    KeyMap& getKeyMap() noexcept { return *keyMap; }
    ProjectController& getProjectController() noexcept { return *projectController; }
    StemSeparationService& getStemSeparation() noexcept { return *stemSeparation; }
    AutomationRecorder& getAutomationRecorder() noexcept { return *automationRecorder; }

    juce::File getCurrentProjectFile() const { return projectFile; }
    void setCurrentProjectFile (const juce::File& f) { projectFile = f; }
    /** Per-project cache directory (waveforms, stems, recordings). */
    juce::File getCacheDirectory() const;

    struct Listener { virtual ~Listener() = default; virtual void projectReplaced() {} };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }
    void notifyProjectReplaced() { listeners.call ([] (Listener& l) { l.projectReplaced(); }); }

private:
    juce::UndoManager undoManager { 30000, 30 };
    ProjectModel project;
    juce::ThreadPool threadPool;
    juce::ApplicationProperties appProperties;
    juce::File projectFile;
    std::unique_ptr<SourceLibrary> sourceLibrary;
    std::unique_ptr<PluginHost> pluginHost;
    std::unique_ptr<EffectFactory> effectFactory;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<WaveformCacheManager> waveformCache;
    std::unique_ptr<AnalysisService> analysis;
    std::unique_ptr<KeyMap> keyMap;
    std::unique_ptr<ProjectController> projectController;
    std::unique_ptr<RecordingController> recordingController;
    std::unique_ptr<StemSeparationService> stemSeparation;
    std::unique_ptr<StemPlacer> stemPlacer;
    std::unique_ptr<AutomationRecorder> automationRecorder;
    juce::ListenerList<Listener> listeners;
};
} // namespace mashup
