#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "UI/Timeline/TimelineViewState.h"
#include "Tracks/TrackModel.h"

namespace mashup::ui
{
class PluginWindows;

/** One effect chain (track / bus / master): slots with bypass, edit, remove; "+" adds built-ins or VST3. */
class EffectChainEditor : public juce::Component, private juce::ValueTree::Listener
{
public:
    EffectChainEditor (Session&, PluginWindows&, juce::ValueTree effectsList);
    ~EffectChainEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void setEffectsList (juce::ValueTree);
private:
    void rebuild();
    void showAddMenu();
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p == list) rebuild(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p == list) rebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree& p, int, int) override { if (p == list) rebuild(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) override { if (v.getParent() == list) repaint(); }
    struct Slot : public juce::Component
    {
        Slot (EffectChainEditor&, juce::ValueTree);
        void resized() override; void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        EffectChainEditor& owner; juce::ValueTree node; juce::TextButton bypass { "B" }, remove { "x" };
    };
    Session& session; PluginWindows& windows; juce::ValueTree list;
    std::vector<std::unique_ptr<Slot>> slots; juce::TextButton addButton { "+ FX" };
};

/** Channel strip for a track, a bus or the master. */
class ChannelStrip : public juce::Component, private juce::ValueTree::Listener, private juce::Timer
{
public:
    enum class Kind { Track, Bus, Master };
    ChannelStrip (Session&, TimelineViewState&, PluginWindows&, juce::ValueTree node, Kind);
    ~ChannelStrip() override;
    void resized() override; void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::ValueTree getNode() const { return node; }
private:
    void refresh();
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) override { if (v == node || v.getParent() == node.getChildWithName (ids::SENDS)) refresh(); }
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (ids::SENDS)) refresh(); }
    juce::UndoManager* um();
    Session& session; TimelineViewState& view; juce::ValueTree node; Kind kind;
    juce::Label name;
    juce::Slider fader, pan, inputGain, sendA, sendB;
    juce::TextButton mute { "M" }, solo { "S" }, phase { "P" }, arm { "R" };
    juce::Label faderValue, lufsLabel;
    std::unique_ptr<EffectChainEditor> chain;
    float meter[2] { 0, 0 }; bool updating = false;
};

/** Bottom tab: all channel strips in a horizontal viewport + master. */
class MixerPanel : public juce::Component, private juce::ValueTree::Listener, private juce::ChangeListener
{
public:
    MixerPanel (Session&, TimelineViewState&);
    ~MixerPanel() override;
    void resized() override; void paint (juce::Graphics&) override;
    PluginWindows& getPluginWindows() { return *windows; }
private:
    void rebuild();
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (ids::TRACKS)) rebuild(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (ids::TRACKS)) rebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree& p, int, int) override { if (p.hasType (ids::TRACKS)) rebuild(); }
    void valueTreeRedirected (juce::ValueTree&) override { rebuild(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    Session& session; TimelineViewState& view;
    std::unique_ptr<PluginWindows> windows;
    juce::Viewport viewport; juce::Component stripHolder;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::unique_ptr<ChannelStrip> master;
};
} // namespace mashup::ui
