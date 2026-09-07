#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"

namespace mashup::ui
{
/** Top strip: transport controls, position (bars.beats + time), BPM, key, time signature, master meter, CPU. */
class TransportBar : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    explicit TransportBar (Session&);
    ~TransportBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void refreshFromModel();
    void editBpm();
    void editKey();
    void editTimeSig();

    Session& session;
    juce::TextButton goStart { "|<" }, rewind { "<<" }, stopButton { "Stop" }, playButton { "Play" }, recordButton { "Rec" }, loopButton { "Loop" };
    juce::TextButton metronomeButton { "Click" }, bypassFxButton { "FX Byp" };
    juce::Label positionBars, positionTime, bpmLabel, keyLabel, timeSigLabel, cpuLabel;
    juce::Rectangle<int> meterArea;
    float meterL = 0.0f, meterR = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
}
