#include "BottomPanel.h"
#include "UI/Theme/Theme.h"
#include "UI/Analysis/AnalysisPanel.h"
#include "UI/Stems/StemsPanel.h"
#include "UI/Mashup/MashupPanel.h"
#include "UI/Mixer/MixerPanel.h"
#include "UI/Chopper/ChopperPanel.h"
#include "UI/Plugins/PluginWindows.h"
#include "Commands/CommandIDs.h"
namespace mashup::ui
{
BottomPanel::BottomPanel (Session& s, TimelineViewState& v) : session (s), view (v)
{
    tabs.addTab ("Mixer", colours::panelBg, mixer = new MixerPanel (session, view), true);
    tabs.addTab ("Analysis", colours::panelBg, new AnalysisPanel (session, view), true);
    tabs.addTab ("Mashup Assistant", colours::panelBg, mashupPanel = new MashupPanel (session, view), true);
    tabs.addTab ("Vocal Chopper", colours::panelBg, chopper = new ChopperPanel (session, view), true);
    tabs.addTab ("Stems", colours::panelBg, stems = new StemsPanel (session, view), true);
    tabs.setTabBarDepth (24);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
}
void BottomPanel::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); }
void BottomPanel::resized() { tabs.setBounds (getLocalBounds()); }
void BottomPanel::showTab (const juce::String& name)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i) if (tabs.getTabNames()[i] == name) { tabs.setCurrentTabIndex (i); return; }
}
bool BottomPanel::performCommand (int id)
{
    switch (id)
    {
        case cmd::vocalChopper: showTab ("Vocal Chopper"); chopper->openWithSelection(); return true;
        case cmd::pluginManager: PluginManagerDialog::show (session); return true;
        case cmd::mashupAssistant: showTab ("Mashup Assistant"); mashupPanel->openWithSelection(); return true;
        case cmd::separateStems: showTab ("Stems"); stems->startForSelection (StemSeparationService::Mode::FourStems); return true;
        case cmd::extractAcapella: showTab ("Stems"); stems->startForSelection (StemSeparationService::Mode::Acapella); return true;
        case cmd::extractInstrumental: showTab ("Stems"); stems->startForSelection (StemSeparationService::Mode::Instrumental); return true;
        default: return false;
    }
}
}
