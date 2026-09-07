#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Project/Session.h"
#include "AudioEngine/AudioEngine.h"

namespace mashup::ui
{
/** Opens/closes editor windows for effect processors living in the current RenderGraph. Windows are closed
    automatically when the graph is rebuilt and the processor instance no longer exists. */
class PluginWindows : private AudioEngine::Listener, private juce::Timer
{
public:
    explicit PluginWindows (Session&);
    ~PluginWindows() override;
    /** effectNode: an EFFECT ValueTree; finds the live processor and shows its editor (toggles if open). */
    void openEditor (const juce::ValueTree& effectNode);
    void closeAll();
    static juce::AudioProcessor* findProcessor (const RenderGraph*, const juce::String& effectId);

private:
    void graphRebuilt() override { startTimer (50); }
    void timerCallback() override;
    class Window;
    Session& session;
    std::vector<std::unique_ptr<Window>> windows;
};

/** Scans and lists VST3 plugins. */
class PluginManagerDialog : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PluginManagerDialog (Session&);
    ~PluginManagerDialog() override;
    void resized() override;
    static void show (Session&);
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    Session& session;
    juce::PluginListComponent* listComponent = nullptr;
    juce::TextButton scanButton { "Scan VST3 folders" }, pathButton { "Set folders..." };
    juce::Label status;
};
} // namespace mashup::ui
