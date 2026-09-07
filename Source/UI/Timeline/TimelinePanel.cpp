#include "TimelinePanel.h"
#include "UI/Theme/Theme.h"
#include "Clips/ClipOperations.h"
#include "Import/SourceLibrary.h"
#include "Import/FFmpegDecoder.h"
#include "AudioEngine/AudioEngine.h"
#include "Analysis/AnalysisService.h"

namespace mashup::ui
{
TimelinePanel::TimelinePanel (Session& s) : session (s)
{
    ruler = std::make_unique<Ruler> (session, view);
    headers = std::make_unique<TrackHeaderList> (session, view);
    canvas = std::make_unique<ArrangementCanvas> (session, view);
    addAndMakeVisible (*ruler); addAndMakeVisible (*headers); addAndMakeVisible (*canvas);
    addAndMakeVisible (hScroll); addAndMakeVisible (vScroll);
    hScroll.addListener (this); vScroll.addListener (this);
    hScroll.setAutoHide (false); vScroll.setAutoHide (false);

    for (auto* b : { &snapButton, &selectTool, &bladeTool, &addTrackButton, &zoomInButton, &zoomOutButton, &fitButton, &followButton }) addAndMakeVisible (*b);
    snapButton.setClickingTogglesState (true); snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.onClick = [this] { view.snapEnabled = snapButton.getToggleState(); view.sendChangeMessage(); };
    selectTool.setRadioGroupId (1); bladeTool.setRadioGroupId (1);
    selectTool.setClickingTogglesState (true); bladeTool.setClickingTogglesState (true);
    selectTool.setToggleState (true, juce::dontSendNotification);
    selectTool.onClick = [this] { setTool (Tool::Select); };
    bladeTool.onClick = [this] { setTool (Tool::Blade); };
    addTrackButton.onClick = [this] { trackops::addTrack (session.getProject(), {}); };
    zoomInButton.onClick = [this] { zoomIn(); };
    zoomOutButton.onClick = [this] { zoomOut(); };
    fitButton.onClick = [this] { zoomToFit(); };
    followButton.setClickingTogglesState (true); followButton.setToggleState (true, juce::dontSendNotification);
    followButton.onClick = [this] { view.followPlayhead = followButton.getToggleState(); };

    addAndMakeVisible (gridBox); addAndMakeVisible (gridLabel);
    gridLabel.setText ("Grid", juce::dontSendNotification); gridLabel.setColour (juce::Label::textColourId, colours::textDim);
    gridBox.addItem ("Adaptive", 1); gridBox.addItem ("Bar", 2); gridBox.addItem ("1/2", 3); gridBox.addItem ("1/4", 4); gridBox.addItem ("1/8", 5); gridBox.addItem ("1/16", 6); gridBox.addItem ("1/32", 7); gridBox.addItem ("1/8T", 8); gridBox.addItem ("1/16T", 9);
    gridBox.setSelectedId (1);
    gridBox.onChange = [this]
    {
        const int id = gridBox.getSelectedId();
        const double bpb = session.getProject().getTimeSigNumerator() * 4.0 / session.getProject().getTimeSigDenominator();
        view.tripletGrid = id >= 8;
        switch (id) { case 1: view.gridDivision = 0; break; case 2: view.gridDivision = bpb; break; case 3: view.gridDivision = 2; break; case 4: view.gridDivision = 1; break; case 5: view.gridDivision = 0.5; break; case 6: view.gridDivision = 0.25; break; case 7: view.gridDivision = 0.125; break; case 8: view.gridDivision = 0.5; break; case 9: view.gridDivision = 0.25; break; }
        view.sendChangeMessage();
    };

    canvas->onFilesDropped = [this] (const juce::StringArray& f, double b, TrackModel t) { importFiles (f, b, t); };
    canvas->onSourceDropped = [this] (const juce::String& id, double b, TrackModel t) { placeSource (id, b, t); };

    view.addChangeListener (this);
    session.getProject().getRoot().addListener (this);
    updateScrollbars();
}

TimelinePanel::~TimelinePanel() { view.removeChangeListener (this); session.getProject().getRoot().removeListener (this); }

void TimelinePanel::setTool (Tool t) { view.tool = t; selectTool.setToggleState (t == Tool::Select, juce::dontSendNotification); bladeTool.setToggleState (t == Tool::Blade, juce::dontSendNotification); view.sendChangeMessage(); }

void TimelinePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panelBg);
    g.setColour (colours::headerBg); g.fillRect (0, 0, getWidth(), 28);
    g.setColour (colours::border); g.drawHorizontalLine (28, 0.0f, (float) getWidth());
    // corner above the headers
    g.setColour (colours::headerBg); g.fillRect (0, 29, view.headerWidth, view.rulerHeight);
}

void TimelinePanel::resized()
{
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop (29).reduced (6, 3);
    auto place = [&] (juce::Component& c, int w) { c.setBounds (toolbar.removeFromLeft (w)); toolbar.removeFromLeft (4); };
    place (selectTool, 58); place (bladeTool, 54); toolbar.removeFromLeft (8);
    place (snapButton, 50); place (gridLabel, 30); place (gridBox, 90); toolbar.removeFromLeft (8);
    place (zoomOutButton, 26); place (zoomInButton, 26); place (fitButton, 40); place (followButton, 56);
    addTrackButton.setBounds (toolbar.removeFromRight (70));

    auto bottom = r.removeFromBottom (12);
    hScroll.setBounds (bottom.withTrimmedLeft (view.headerWidth).withTrimmedRight (12));
    auto right = r.removeFromRight (12);
    vScroll.setBounds (right.withTrimmedTop (view.rulerHeight));
    auto rulerRow = r.removeFromTop (view.rulerHeight);
    ruler->setBounds (rulerRow.withTrimmedLeft (view.headerWidth));
    headers->setBounds (r.removeFromLeft (view.headerWidth));
    canvas->setBounds (r);
    updateScrollbars();
}

void TimelinePanel::updateScrollbars()
{
    if (updatingScroll) return;
    updatingScroll = true;
    const double visibleBeats = canvas->getWidth() / view.pixelsPerBeat;
    const double contentEnd = juce::jmax (canvas->getContentEndBeat() + visibleBeats * 0.5, view.scrollBeat + visibleBeats, visibleBeats * 2);
    hScroll.setRangeLimits (0.0, contentEnd);
    hScroll.setCurrentRange (view.scrollBeat, visibleBeats, juce::dontSendNotification);
    const int totalH = canvas->getTotalTracksHeight() + 60;
    vScroll.setRangeLimits (0.0, juce::jmax (totalH, canvas->getHeight()));
    vScroll.setCurrentRange (view.verticalScroll, canvas->getHeight(), juce::dontSendNotification);
    updatingScroll = false;
}

void TimelinePanel::scrollBarMoved (juce::ScrollBar* sb, double newStart)
{
    if (updatingScroll) return;
    updatingScroll = true;
    if (sb == &hScroll) view.setScrollBeat (newStart);
    else view.setVerticalScroll ((int) newStart);
    updatingScroll = false;
    headers->resized();
}

void TimelinePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateScrollbars();
    snapButton.setToggleState (view.snapEnabled, juce::dontSendNotification);
    headers->resized();
}

void TimelinePanel::zoomIn()  { view.setZoom (view.pixelsPerBeat * 1.5, canvas->getWidth() * 0.5); }
void TimelinePanel::zoomOut() { view.setZoom (view.pixelsPerBeat / 1.5, canvas->getWidth() * 0.5); }
void TimelinePanel::zoomToFit()
{
    const double end = juce::jmax (8.0, canvas->getContentEndBeat());
    view.scrollBeat = 0.0;
    view.setZoom (juce::jmax (0.5, (canvas->getWidth() - 20) / end), 0.0);
}
void TimelinePanel::zoomToSelection()
{
    if (! view.hasTimeSelection) return;
    const double len = juce::jmax (0.25, view.timeSelEnd - view.timeSelStart);
    view.scrollBeat = view.timeSelStart;
    view.setZoom ((canvas->getWidth() - 20) / len, 0.0);
}

void TimelinePanel::importFiles (const juce::StringArray& files, double beat, TrackModel track)
{
    auto& p = session.getProject();
    auto& lib = session.getSourceLibrary();
    session.getUndoManager().beginNewTransaction ("Import audio");
    bool first = true;
    for (auto& path : files)
    {
        juce::File f (path);
        if (! f.existsAsFile() || ! FFmpegDecoder::isSupportedExtension (f.getFileExtension())) continue;
        FFmpegDecoder::Info info; juce::String err;
        if (! FFmpegDecoder::probe (f, info, err)) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", f.getFileName() + "\n" + err); continue; }
        const auto id = lib.importFile (f);
        const double seconds = info.sampleRate > 0 ? info.estimatedLength / info.sampleRate : 0.0;
        TrackModel target = first && track.isValid() ? track : trackops::addTrack (p, f.getFileNameWithoutExtension());
        first = false;
        auto clip = trackops::placeSource (p, id, f.getFileNameWithoutExtension(), seconds, beat, target);
        session.getAnalysis().analyseSource (id);
        view.selectClip (clip.getId());
    }
}

void TimelinePanel::placeSource (const juce::String& sourceId, double beat, TrackModel track)
{
    auto& p = session.getProject();
    auto node = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, sourceId);
    if (! node.isValid()) return;
    const double seconds = (double) node[ids::lengthSamples] / juce::jmax (1.0, (double) node[ids::sampleRate]);
    session.getUndoManager().beginNewTransaction ("Place source");
    if (! track.isValid()) track = trackops::addTrack (p, node[ids::name].toString());
    auto clip = trackops::placeSource (p, sourceId, node[ids::name].toString(), seconds, beat, track, (double) node.getProperty (ids::bpm, 0.0));
    if (node.hasProperty (ids::keyRoot)) clip.setKey ((int) node[ids::keyRoot], (int) node[ids::keyMode], nullptr);
    view.selectClip (clip.getId());
}
} // namespace mashup::ui
