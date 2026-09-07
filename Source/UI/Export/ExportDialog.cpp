#include "ExportDialog.h"
#include "UI/Theme/Theme.h"
#include "Tracks/TrackModel.h"
#include "AudioEngine/AudioEngine.h"

namespace mashup::ui
{
ExportDialog::ExportDialog (Session& s, TimelineViewState& v, What w) : Thread ("export"), session (s), view (v), what (w)
{
    auto lab = [this] (juce::Label& l, const char* t) { addAndMakeVisible (l); l.setText (t, juce::dontSendNotification); l.setFont (Theme::ui (12.0f)); l.setColour (juce::Label::textColourId, colours::textDim); };
    lab (whatLabel, "Export"); lab (formatLabel, "Format"); lab (rateLabel, "Sample rate"); lab (depthLabel, "Bit depth"); lab (bitrateLabel, "Bitrate"); lab (rangeLabel, "Range"); lab (fileLabel, "File"); lab (statusLabel, "");
    for (auto* b : { &whatBox, &formatBox, &rateBox, &depthBox, &bitrateBox, &rangeBox }) addAndMakeVisible (*b);
    whatBox.addItem ("Master mix", 1); whatBox.addItem ("Each track as a file", 2); whatBox.addItem ("Stems: Vocals / Drums / Bass / Instrumental", 3);
    whatBox.setSelectedId (w == What::Master ? 1 : w == What::EachTrack ? 2 : 3);
    formatBox.addItem ("WAV", 1); formatBox.addItem ("FLAC", 2); formatBox.addItem ("MP3", 3); formatBox.addItem ("AAC (m4a)", 4); formatBox.setSelectedId (1);
    formatBox.onChange = [this] { const int f = formatBox.getSelectedId(); depthBox.setEnabled (f <= 2); bitrateBox.setEnabled (f >= 3); auto t = fileEditor.getText(); if (t.isNotEmpty()) fileEditor.setText (juce::File (t).withFileExtension (FFmpegEncoder::extensionFor ((FFmpegEncoder::Format) (f - 1))).getFullPathName()); };
    for (int r : { 44100, 48000, 88200, 96000 }) rateBox.addItem (juce::String (r) + " Hz", r);
    rateBox.setSelectedId ((int) session.getAudioEngine().getSampleRate() == 44100 ? 44100 : 48000);
    depthBox.addItem ("16 bit", 16); depthBox.addItem ("24 bit", 24); depthBox.addItem ("32 bit float", 32); depthBox.setSelectedId (24);
    for (int b : { 128, 192, 256, 320 }) bitrateBox.addItem (juce::String (b) + " kbps", b); bitrateBox.setSelectedId (320); bitrateBox.setEnabled (false);
    rangeBox.addItem ("Whole project", 1); rangeBox.addItem ("Loop region", 2); rangeBox.addItem ("Time selection", 3); rangeBox.setSelectedId (1);
    for (auto* t : { &monoToggle, &normPeak, &normLufs }) addAndMakeVisible (*t);
    addAndMakeVisible (peakValue); peakValue.setRange (-12.0, 0.0, 0.1); peakValue.setValue (-1.0); peakValue.setTextValueSuffix (" dBFS"); peakValue.setSliderStyle (juce::Slider::LinearHorizontal); peakValue.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 18);
    addAndMakeVisible (lufsValue); lufsValue.setRange (-24.0, -6.0, 0.5); lufsValue.setValue (-14.0); lufsValue.setTextValueSuffix (" LUFS"); lufsValue.setSliderStyle (juce::Slider::LinearHorizontal); lufsValue.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 18);
    addAndMakeVisible (fileEditor); addAndMakeVisible (browse); addAndMakeVisible (exportButton); addAndMakeVisible (cancelButton); addAndMakeVisible (bar);
    auto defaultDir = session.getCurrentProjectFile() != juce::File() ? session.getCurrentProjectFile().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileEditor.setText (defaultDir.getChildFile (session.getProject().getName() + ".wav").getFullPathName());
    browse.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Export to", juce::File (fileEditor.getText()), "*" + juce::String (FFmpegEncoder::extensionFor ((FFmpegEncoder::Format) (formatBox.getSelectedId() - 1))));
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) fileEditor.setText (fc.getResult().getFullPathName()); });
    };
    exportButton.onClick = [this] { startExport(); };
    cancelButton.onClick = [this] { if (isThreadRunning()) cancelFlag.store (true); else if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };
    setSize (560, 420);
}

ExportDialog::~ExportDialog() { cancelFlag.store (true); stopThread (10000); }

void ExportDialog::show (Session& s, TimelineViewState& v, What w)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ExportDialog (s, v, w));
    o.dialogTitle = "Export"; o.dialogBackgroundColour = colours::panelBg; o.useNativeTitleBar = true; o.resizable = false;
    o.launchAsync();
}

std::vector<ExportDialog::Item> ExportDialog::buildItems() const
{
    std::vector<Item> items;
    auto& p = session.getProject();
    const int w = whatBox.getSelectedId();
    if (w == 1) { items.push_back ({ "", {} }); return items; }
    if (w == 2) { for (auto t : p.tracks()) { TrackModel tm (t); items.push_back ({ tm.getName(), { tm.getId() } }); } return items; }
    std::map<juce::String, std::set<juce::String>> groups;
    for (auto t : p.tracks())
    {
        TrackModel tm (t);
        juce::String stem = tm.getStemType();
        if (stem.isEmpty()) for (auto c : tm.clips()) { auto src = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, c[ids::sourceId].toString()); if (src.hasProperty (ids::stemType)) stem = src[ids::stemType].toString(); }
        juce::String group = stem == "vocals" ? "Vocals" : stem == "drums" ? "Drums" : stem == "bass" ? "Bass" : "Instrumental";
        groups[group].insert (tm.getId());
    }
    for (auto& [name, ids_] : groups) items.push_back ({ name, ids_ });
    return items;
}

void ExportDialog::startExport()
{
    if (isThreadRunning()) return;
    baseFile = juce::File (fileEditor.getText());
    if (baseFile.getFullPathName().isEmpty()) return;
    enc.format = (FFmpegEncoder::Format) (formatBox.getSelectedId() - 1);
    enc.sampleRate = rateBox.getSelectedId(); enc.bitDepth = depthBox.getSelectedId(); enc.bitrateKbps = bitrateBox.getSelectedId(); enc.mono = monoToggle.getToggleState();
    baseFile = baseFile.withFileExtension (FFmpegEncoder::extensionFor (enc.format));
    opts = {}; opts.sampleRate = enc.sampleRate;
    opts.normalizePeak = normPeak.getToggleState(); opts.peakDbfs = peakValue.getValue();
    opts.normalizeLoudness = normLufs.getToggleState(); opts.targetLufs = lufsValue.getValue();
    auto& p = session.getProject();
    if (rangeBox.getSelectedId() == 2 && p.getLoopEnd() > p.getLoopStart()) { opts.startBeat = p.getLoopStart(); opts.endBeat = p.getLoopEnd(); }
    else if (rangeBox.getSelectedId() == 3 && view.hasTimeSelection) { opts.startBeat = view.timeSelStart; opts.endBeat = view.timeSelEnd; }
    items = buildItems();
    cancelFlag.store (false); finished.store (false); progressAtomic.store (0.0f);
    exportButton.setEnabled (false); statusLabel.setText ("Rendering...", juce::dontSendNotification);
    startThread(); startTimerHz (10);
}

void ExportDialog::run()
{
    juce::String errors;
    const int n = (int) items.size();
    for (int i = 0; i < n && ! cancelFlag.load(); ++i)
    {
        auto o = opts; o.onlyTracks = items[(size_t) i].tracks;
        juce::File f = items[(size_t) i].name.isEmpty() ? baseFile : baseFile.getSiblingFile (baseFile.getFileNameWithoutExtension() + " - " + juce::File::createLegalFileName (items[(size_t) i].name) + baseFile.getFileExtension());
        auto err = OfflineRenderer::renderToFile (session, o, f, enc, [this, i, n] (float p) { progressAtomic.store ((i + p) / (float) n); return ! cancelFlag.load(); });
        if (err.isNotEmpty()) errors << f.getFileName() << ": " << err << "\n";
    }
    resultMessage = cancelFlag.load() ? "Cancelled" : errors.isEmpty() ? "Exported " + juce::String (n) + " file(s) to " + baseFile.getParentDirectory().getFullPathName() : errors;
    finished.store (true);
}

void ExportDialog::timerCallback()
{
    progress = progressAtomic.load();
    if (finished.load())
    {
        stopTimer(); exportButton.setEnabled (true); progress = 1.0;
        statusLabel.setText (resultMessage, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, resultMessage.startsWith ("Exported") ? colours::meterLow : colours::danger);
        cancelButton.setButtonText ("Close");
    }
}

void ExportDialog::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); }

void ExportDialog::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto row = [&] (juce::Label& l, juce::Component& c) { auto rr = r.removeFromTop (28); l.setBounds (rr.removeFromLeft (90)); c.setBounds (rr.reduced (0, 3)); };
    row (whatLabel, whatBox); row (formatLabel, formatBox); row (rateLabel, rateBox);
    { auto rr = r.removeFromTop (28); depthLabel.setBounds (rr.removeFromLeft (90)); depthBox.setBounds (rr.removeFromLeft (140).reduced (0, 3)); rr.removeFromLeft (10); bitrateLabel.setBounds (rr.removeFromLeft (50)); bitrateBox.setBounds (rr.removeFromLeft (110).reduced (0, 3)); monoToggle.setBounds (rr.withTrimmedLeft (10)); }
    row (rangeLabel, rangeBox);
    { auto rr = r.removeFromTop (28); normPeak.setBounds (rr.removeFromLeft (160)); peakValue.setBounds (rr); }
    { auto rr = r.removeFromTop (28); normLufs.setBounds (rr.removeFromLeft (160)); lufsValue.setBounds (rr); }
    { auto rr = r.removeFromTop (28); fileLabel.setBounds (rr.removeFromLeft (90)); browse.setBounds (rr.removeFromRight (34).reduced (2)); fileEditor.setBounds (rr.reduced (0, 3)); }
    r.removeFromTop (8);
    bar.setBounds (r.removeFromTop (20));
    statusLabel.setBounds (r.removeFromTop (44));
    auto buttons = r.removeFromBottom (30);
    cancelButton.setBounds (buttons.removeFromRight (100)); buttons.removeFromRight (8); exportButton.setBounds (buttons.removeFromRight (120));
}
} // namespace mashup::ui
