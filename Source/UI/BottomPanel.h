#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "UI/Timeline/TimelineViewState.h"

namespace mashup::ui
{
class StemsPanel;
class MashupPanel;
class MixerPanel;
class ChopperPanel;
/** Tabbed bottom area: Mixer / Vocal Chopper / Mashup Assistant / Analysis. Tabs are added as their
    features come online; performCommand routes tool commands to the owning tab. */
class BottomPanel : public juce::Component
{
public:
    BottomPanel (Session&, TimelineViewState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::TabbedComponent& getTabs() { return tabs; }
    void showTab (const juce::String& name);
    bool performCommand (int commandId);
private:
    Session& session;
    TimelineViewState& view;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    StemsPanel* stems = nullptr;
    MashupPanel* mashupPanel = nullptr;
    MixerPanel* mixer = nullptr;
    ChopperPanel* chopper = nullptr;
};
}
