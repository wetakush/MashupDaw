#include "PluginWindows.h"
#include "UI/Theme/Theme.h"
#include "Effects/ProcessorChain.h"
#include "Effects/BuiltInEffect.h"
#include "Plugins/PluginHost.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace mashup::ui
{
class PluginWindows::Window : public juce::DocumentWindow, private juce::AudioProcessorListener, private juce::Timer
{
public:
    Window (PluginWindows& o, Session& s, juce::AudioProcessor* p, juce::ValueTree node)
        : DocumentWindow (node[ids::name].toString(), colours::panelBg, DocumentWindow::closeButton), owner (o), session (s), processor (p), effectNode (node)
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* editor = p->hasEditor() ? p->createEditorIfNeeded() : nullptr;
        if (! editor) editor = new juce::GenericAudioProcessorEditor (*p);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreAroundComponent (nullptr, getWidth(), getHeight());
        processor->addListener (this);
        setVisible (true);
        startTimer (700);
    }
    ~Window() override { if (processor) processor->removeListener (this); clearContentComponent(); }
    void closeButtonPressed() override { owner.windows.erase (std::remove_if (owner.windows.begin(), owner.windows.end(), [this] (auto& w) { return w.get() == this; }), owner.windows.end()); }
    juce::AudioProcessor* processor;
    juce::ValueTree effectNode;
private:
    // persist parameter/state edits into the model so they are saved and undoable (built-ins: PARAMS; plugins: state blob)
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { dirty = true; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override { dirty = true; }
    void timerCallback() override
    {
        if (! dirty || ! processor) return;
        dirty = false;
        if (auto* b = dynamic_cast<BuiltInEffect*> (processor)) b->saveParamsToNode (effectNode, nullptr);
        else { juce::MemoryBlock mb; processor->getStateInformation (mb); effectNode.setProperty (ids::state, mb.toBase64Encoding(), nullptr); }
    }
    PluginWindows& owner; Session& session; bool dirty = false;
};

PluginWindows::PluginWindows (Session& s) : session (s) { session.getAudioEngine().addListener (this); }
PluginWindows::~PluginWindows() { session.getAudioEngine().removeListener (this); closeAll(); }
void PluginWindows::closeAll() { windows.clear(); }

juce::AudioProcessor* PluginWindows::findProcessor (const RenderGraph* g, const juce::String& effectId)
{
    if (! g) return nullptr;
    auto search = [&] (ProcessorChain* c) -> juce::AudioProcessor* { if (! c) return nullptr; for (auto& s : c->slots) if (s->effectId == effectId) return s->processor.get(); return nullptr; };
    for (auto& t : g->tracks) if (auto* p = search (t->getEffectChain())) return p;
    if (auto* p = search (g->masterChain.get())) return p;
    for (auto& b : g->busChains) if (auto* p = search (b.get())) return p;
    return nullptr;
}

void PluginWindows::openEditor (const juce::ValueTree& node)
{
    const auto id = node[ids::id].toString();
    for (auto it = windows.begin(); it != windows.end(); ++it)
        if ((*it)->effectNode[ids::id].toString() == id) { windows.erase (it); return; }
    auto* proc = findProcessor (session.getAudioEngine().getCurrentGraph(), id);
    if (! proc) return;
    windows.push_back (std::make_unique<Window> (*this, session, proc, node));
}

void PluginWindows::timerCallback()
{
    stopTimer();
    auto* g = session.getAudioEngine().getCurrentGraph();
    windows.erase (std::remove_if (windows.begin(), windows.end(), [&] (auto& w) { return findProcessor (g, w->effectNode[ids::id].toString()) != w->processor; }), windows.end());
}

// ---- plugin manager --------------------------------------------------------------------------------------------------------
PluginManagerDialog::PluginManagerDialog (Session& s) : session (s)
{
    auto& host = session.getPluginHost();
    listComponent = new juce::PluginListComponent (host.getFormatManager(), host.getKnownPlugins(), juce::File(), session.getAppProperties().getUserSettings(), true);
    addAndMakeVisible (listComponent);
    addAndMakeVisible (scanButton); addAndMakeVisible (pathButton); addAndMakeVisible (status);
    status.setFont (Theme::ui (11.0f)); status.setColour (juce::Label::textColourId, colours::textDim);
    scanButton.onClick = [this] { session.getPluginHost().scanAsync (false); startTimerHz (5); };
    pathButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("VST3 folder", juce::File ("/usr/lib/vst3"));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser& fc)
        { if (fc.getResult() != juce::File()) { auto p = session.getPluginHost().getSearchPath(); p.add (fc.getResult()); session.getPluginHost().setSearchPath (p); refresh(); } });
    };
    host.addChangeListener (this);
    refresh();
    setSize (700, 500);
}
PluginManagerDialog::~PluginManagerDialog() { session.getPluginHost().removeChangeListener (this); delete listComponent; }
void PluginManagerDialog::show (Session& s)
{
    juce::DialogWindow::LaunchOptions o; o.content.setOwned (new PluginManagerDialog (s)); o.dialogTitle = "Plugin manager"; o.dialogBackgroundColour = colours::panelBg; o.useNativeTitleBar = true; o.resizable = true; o.launchAsync();
}
void PluginManagerDialog::timerCallback() { refresh(); if (! session.getPluginHost().isScanning()) stopTimer(); }
void PluginManagerDialog::refresh()
{
    auto& host = session.getPluginHost();
    status.setText (host.isScanning() ? "Scanning " + juce::String ((int) (host.getScanProgress() * 100)) + "%  " + host.getCurrentlyScanning() : juce::String (host.getKnownPlugins().getNumTypes()) + " plugins.  Paths: " + host.getSearchPath().toString(), juce::dontSendNotification);
}
void PluginManagerDialog::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (26); scanButton.setBounds (top.removeFromLeft (150)); top.removeFromLeft (6); pathButton.setBounds (top.removeFromLeft (110)); status.setBounds (top.withTrimmedLeft (10));
    r.removeFromTop (6); listComponent->setBounds (r);
}
} // namespace mashup::ui
