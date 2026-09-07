#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Project/Session.h"
#include "UI/Timeline/TimelineViewState.h"
#include "Clips/ClipModel.h"

namespace mashup::ui
{
/** Vocal Chopper: turns the selected clip(s) into slices shown as pads. Pads audition, can be dragged onto each
    other to swap positions on the timeline, and are triggered by keyboard (1-0, Q-P) and MIDI notes (C1 = pad 1). */
class ChopperPanel : public juce::Component, private juce::ChangeListener, private juce::ValueTree::Listener,
                     private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer
{
public:
    ChopperPanel (Session&, TimelineViewState&);
    ~ChopperPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void openWithSelection();

private:
    struct Pad : public juce::Component
    {
        Pad (ChopperPanel& o, int index) : owner (o), idx (index) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        ChopperPanel& owner; int idx; bool dragging = false, hot = false;
    };
    void changeListenerCallback (juce::ChangeBroadcaster*) override { if (! following) return; refreshFromSelection(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) override { if (v.hasType (ids::CLIP)) triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuildPads(); }
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;

    void refreshFromSelection();
    void rebuildPads();
    void trigger (int pad);
    void swapPads (int a, int b);
    void sliceSelected (int mode);
    std::vector<ClipModel> slices() const;
    void showPadMenu (int pad, const juce::MouseEvent&);

    Session& session; TimelineViewState& view;
    juce::String trackId; std::vector<juce::String> sliceIds;
    std::vector<std::unique_ptr<Pad>> pads;
    juce::Label title, hint;
    juce::TextButton sliceTransients { "Slice: transients" }, slice8 { "1/8" }, slice16 { "1/16" }, sliceBeat { "1/4" }, reverseAll { "Reverse all" }, shuffle { "Shuffle" }, restore { "Restore order" }, followButton { "Follow selection" }, midiButton { "MIDI in" };
    bool following = true; int lastTriggered = -1; std::atomic<int> pendingMidiPad { -1 };
    int dragFrom = -1, dragOver = -1;
    juce::TooltipWindow* tips = nullptr;
};
}
