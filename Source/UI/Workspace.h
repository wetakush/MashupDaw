#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"

namespace mashup::ui
{
class TransportBar;
class BrowserPanel;
class TimelinePanel;
class InspectorPanel;
class BottomPanel;

/** Main layout + application command target + menu bar model. */
class Workspace : public juce::Component, public juce::ApplicationCommandTarget, public juce::MenuBarModel
{
public:
    explicit Workspace (Session&);
    ~Workspace() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    TimelinePanel& getTimeline() { return *timeline; }
    BottomPanel& getBottomPanel() { return *bottom; }
    InspectorPanel& getInspector() { return *inspector; }
    BrowserPanel& getBrowser() { return *browser; }
    TransportBar& getTransport() { return *transport; }

    void setBrowserVisible (bool);
    void setInspectorVisible (bool);
    void setBottomVisible (bool);
    void importFilesAtPlayhead (const juce::StringArray& files);

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

private:
    void showAudioSettings();
    void tapTempo();

    Session& session;
    std::unique_ptr<TransportBar> transport;
    std::unique_ptr<BrowserPanel> browser;
    std::unique_ptr<TimelinePanel> timeline;
    std::unique_ptr<InspectorPanel> inspector;
    std::unique_ptr<BottomPanel> bottom;

    juce::StretchableLayoutManager hLayout, vLayout;
    juce::StretchableLayoutResizerBar leftBar { &hLayout, 1, true }, rightBar { &hLayout, 3, true }, bottomBar { &vLayout, 1, false };
    bool browserVisible = true, inspectorVisible = true, bottomVisible = true;
    juce::Component centreHolder;
    std::unique_ptr<juce::DialogWindow> settingsWindow;
    std::vector<double> tapTimes;
};
} // namespace mashup::ui
