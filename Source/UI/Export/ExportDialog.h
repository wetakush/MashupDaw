#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "UI/Timeline/TimelineViewState.h"
#include "Export/OfflineRenderer.h"

namespace mashup::ui
{
/** Export dialog: master mix or stems, format/rate/depth/bitrate, range, normalisation, progress + cancel. */
class ExportDialog : public juce::Component, private juce::Thread, private juce::Timer
{
public:
    enum class What { Master, EachTrack, StemGroups };
    ExportDialog (Session&, TimelineViewState&, What);
    ~ExportDialog() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    static void show (Session&, TimelineViewState&, What);

private:
    void run() override;
    void timerCallback() override;
    void startExport();
    struct Item { juce::String name; std::set<juce::String> tracks; };
    std::vector<Item> buildItems() const;

    Session& session; TimelineViewState& view; What what;
    juce::Label whatLabel, formatLabel, rateLabel, depthLabel, bitrateLabel, rangeLabel, fileLabel, statusLabel;
    juce::ComboBox whatBox, formatBox, rateBox, depthBox, bitrateBox, rangeBox;
    juce::ToggleButton monoToggle { "Mono" }, normPeak { "Normalize peak to" }, normLufs { "Loudness target (LUFS)" };
    juce::Slider peakValue, lufsValue;
    juce::TextEditor fileEditor;
    juce::TextButton browse { "..." }, exportButton { "Export" }, cancelButton { "Cancel" };
    double progress = 0.0; juce::ProgressBar bar { progress };
    std::atomic<float> progressAtomic { 0.0f }; std::atomic<bool> cancelFlag { false };
    juce::String resultMessage; std::atomic<bool> finished { false };
    // captured settings for the thread
    OfflineRenderer::Options opts; FFmpegEncoder::Settings enc; std::vector<Item> items; juce::File baseFile;
};
}
