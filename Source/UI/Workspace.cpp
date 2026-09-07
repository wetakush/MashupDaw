#include "Workspace.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "UI/Theme/Theme.h"
#include "UI/Transport/TransportBar.h"
#include "UI/Browser/BrowserPanel.h"
#include "UI/Timeline/TimelinePanel.h"
#include "UI/Inspector/InspectorPanel.h"
#include "UI/BottomPanel.h"
#include "AudioEngine/AudioEngine.h"
#include "Commands/CommandIDs.h"
#include "Commands/KeyMap.h"
#include "Project/ProjectController.h"
#include "Project/ProjectFile.h"
#include "Clips/ClipOperations.h"
#include "Import/FFmpegDecoder.h"
#include "Analysis/AnalysisService.h"
#include "StemSeparation/StemSeparationService.h"
#include "Slicing/Slicer.h"
#include "UI/Export/ExportDialog.h"

namespace mashup::ui
{
static bool skipPart (const char* name)   // developer bisecting aid: MASHUP_SKIP=browser,inspector,...
{
    if (const char* e = std::getenv ("MASHUP_SKIP")) return juce::StringArray::fromTokens (e, ",", "").contains (name);
    return false;
}

Workspace::Workspace (Session& s) : session (s)
{
    transport = std::make_unique<TransportBar> (session);
    if (! skipPart ("browser")) browser = std::make_unique<BrowserPanel> (session);
    timeline  = std::make_unique<TimelinePanel> (session);
    inspector = std::make_unique<InspectorPanel> (session, timeline->getView());
    bottom    = std::make_unique<BottomPanel> (session, timeline->getView());
    if (! browser) { browserVisible = false; }

    if (browser)
    {
        browser->onImportFiles = [this] (const juce::StringArray& files) { importFilesAtPlayhead (files); };
        browser->onPlaceSource = [this] (const juce::String& id) { timeline->placeSource (id, session.getAudioEngine().getPositionBeat(), {}); };
        browser->onSeparateStems = [this] (const juce::String& id) { setBottomVisible (true); bottom->showTab ("Stems"); session.getStemSeparation().separate (id, StemSeparationService::Mode::FourStems); };
        centreHolder.addAndMakeVisible (*browser);
    }
    addAndMakeVisible (*transport);
    addAndMakeVisible (centreHolder);
    centreHolder.addAndMakeVisible (leftBar);
    centreHolder.addAndMakeVisible (*timeline);
    centreHolder.addAndMakeVisible (rightBar);
    centreHolder.addAndMakeVisible (*inspector);
    addAndMakeVisible (bottomBar);
    addAndMakeVisible (*bottom);

    hLayout.setItemLayout (0, 160, 480, 240);
    hLayout.setItemLayout (1, 4, 4, 4);
    hLayout.setItemLayout (2, 300, -1.0, -0.6);
    hLayout.setItemLayout (3, 4, 4, 4);
    hLayout.setItemLayout (4, 220, 480, 300);
    vLayout.setItemLayout (0, 200, -1.0, -0.62);
    vLayout.setItemLayout (1, 4, 4, 4);
    vLayout.setItemLayout (2, 120, 600, 260);

    if (skipPart ("commands")) return;
    auto& cm = session.getKeyMap().getCommandManager();
    cm.registerAllCommandsForTarget (this);
    cm.setFirstCommandTarget (this);
    session.getKeyMap().applyDefaultsAndLoad();
    addKeyListener (cm.getKeyMappings());
    setWantsKeyboardFocus (true);
}

Workspace::~Workspace() { session.getKeyMap().getCommandManager().setFirstCommandTarget (nullptr); }

void Workspace::paint (juce::Graphics& g) { g.fillAll (colours::windowBg); }

void Workspace::setBrowserVisible (bool v)   { if (! browser) return; browserVisible = v;   browser->setVisible (v);   leftBar.setVisible (v);   resized(); }
void Workspace::setInspectorVisible (bool v) { inspectorVisible = v; inspector->setVisible (v); rightBar.setVisible (v);  resized(); }
void Workspace::setBottomVisible (bool v)    { bottomVisible = v;    bottom->setVisible (v);    bottomBar.setVisible (v); resized(); }

void Workspace::resized()
{
    auto r = getLocalBounds();
    transport->setBounds (r.removeFromTop (56));
    if (bottomVisible)
    {
        juce::Component* vItems[] = { &centreHolder, &bottomBar, bottom.get() };
        vLayout.layOutComponents (vItems, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);
    }
    else centreHolder.setBounds (r);

    auto c = centreHolder.getLocalBounds();
    if (browserVisible && inspectorVisible)
    {
        juce::Component* hItems[] = { browser.get(), &leftBar, timeline.get(), &rightBar, inspector.get() };
        hLayout.layOutComponents (hItems, 5, c.getX(), c.getY(), c.getWidth(), c.getHeight(), false, true);
    }
    else
    {
        if (browserVisible)   { browser->setBounds (c.removeFromLeft (hLayout.getItemCurrentAbsoluteSize (0))); leftBar.setBounds (c.removeFromLeft (4)); }
        if (inspectorVisible) { inspector->setBounds (c.removeFromRight (hLayout.getItemCurrentAbsoluteSize (4))); rightBar.setBounds (c.removeFromRight (4)); }
        timeline->setBounds (c);
    }
}

void Workspace::importFilesAtPlayhead (const juce::StringArray& files)
{
    timeline->importFiles (files, timeline->getView().snap (session.getAudioEngine().getPositionBeat(), 4.0), {});
}

// ---- commands ---------------------------------------------------------------------------------------------------------
void Workspace::getAllCommands (juce::Array<juce::CommandID>& c)
{
    c.addArray ({ cmd::newProject, cmd::openProject, cmd::saveProject, cmd::saveProjectAs, cmd::importAudio, cmd::exportMix, cmd::exportStems, cmd::audioSettings, cmd::editShortcuts, cmd::pluginManager,
                  cmd::undo, cmd::redo, cmd::cut, cmd::copy, cmd::paste, cmd::deleteSelection, cmd::selectAll, cmd::duplicate, cmd::split,
                  cmd::playStop, cmd::stop, cmd::record, cmd::goToStart, cmd::toggleLoop, cmd::setLoopToSelection,
                  cmd::zoomIn, cmd::zoomOut, cmd::zoomToFit, cmd::zoomToSelection, cmd::toggleBrowser, cmd::toggleInspector, cmd::toggleBottom, cmd::showMixer, cmd::showChopper, cmd::showMashupAssistant, cmd::showAnalysis, cmd::toggleFollow,
                  cmd::addTrack, cmd::deleteTrack, cmd::muteTrack, cmd::soloTrack, cmd::armTrack, cmd::duplicateTrack,
                  cmd::toolSelect, cmd::toolBlade, cmd::toolAutomation, cmd::toggleSnap, cmd::matchClipBpm, cmd::matchClipKey, cmd::reverseClip, cmd::loopClip, cmd::muteClip, cmd::normalizeClip,
                  cmd::sliceBeats, cmd::sliceTransients, cmd::slicePhrases, cmd::cropToSelection, cmd::crossfadeSelected,
                  cmd::mashupAssistant, cmd::separateStems, cmd::extractAcapella, cmd::extractInstrumental, cmd::analyseSelected, cmd::vocalChopper, cmd::tapTempo, cmd::bypassEffects });
}

void Workspace::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    auto set = [&] (const char* name, const char* cat, const char* desc = "") { info.setInfo (name, desc[0] ? desc : name, cat, 0); };
    auto& view = timeline->getView();
    const bool hasClip = ! view.selectedClips.empty();
    switch (id)
    {
        case cmd::newProject: set ("New project", "File"); break;
        case cmd::openProject: set ("Open project...", "File"); break;
        case cmd::saveProject: set ("Save", "File"); break;
        case cmd::saveProjectAs: set ("Save as...", "File"); break;
        case cmd::importAudio: set ("Import audio...", "File"); break;
        case cmd::exportMix: set ("Export mix...", "File"); break;
        case cmd::exportStems: set ("Export stems...", "File"); break;
        case cmd::audioSettings: set ("Audio settings...", "File"); break;
        case cmd::editShortcuts: set ("Keyboard shortcuts...", "File"); break;
        case cmd::pluginManager: set ("Plugin manager...", "File"); break;
        case cmd::undo: set ("Undo", "Edit"); info.setActive (session.getUndoManager().canUndo()); break;
        case cmd::redo: set ("Redo", "Edit"); info.setActive (session.getUndoManager().canRedo()); break;
        case cmd::cut: set ("Cut", "Edit"); info.setActive (hasClip); break;
        case cmd::copy: set ("Copy", "Edit"); info.setActive (hasClip); break;
        case cmd::paste: set ("Paste at playhead", "Edit"); break;
        case cmd::deleteSelection: set ("Delete", "Edit"); break;
        case cmd::selectAll: set ("Select all clips", "Edit"); break;
        case cmd::duplicate: set ("Duplicate", "Edit"); info.setActive (hasClip); break;
        case cmd::split: set ("Split at playhead", "Edit"); break;
        case cmd::playStop: set ("Play / Pause", "Transport"); break;
        case cmd::stop: set ("Stop", "Transport"); break;
        case cmd::record: set ("Record", "Transport"); break;
        case cmd::goToStart: set ("Go to start", "Transport"); break;
        case cmd::toggleLoop: set ("Toggle loop", "Transport"); break;
        case cmd::setLoopToSelection: set ("Set loop to selection", "Transport"); info.setActive (view.hasTimeSelection); break;
        case cmd::zoomIn: set ("Zoom in", "View"); break;
        case cmd::zoomOut: set ("Zoom out", "View"); break;
        case cmd::zoomToFit: set ("Zoom to fit", "View"); break;
        case cmd::zoomToSelection: set ("Zoom to selection", "View"); break;
        case cmd::toggleBrowser: set ("Show browser", "View"); info.setTicked (browserVisible); break;
        case cmd::toggleInspector: set ("Show inspector", "View"); info.setTicked (inspectorVisible); break;
        case cmd::toggleBottom: set ("Show bottom panel", "View"); info.setTicked (bottomVisible); break;
        case cmd::showMixer: set ("Mixer", "View"); break;
        case cmd::showChopper: set ("Vocal Chopper", "View"); break;
        case cmd::showMashupAssistant: set ("Mashup Assistant", "View"); break;
        case cmd::showAnalysis: set ("Analysis", "View"); break;
        case cmd::toggleFollow: set ("Follow playhead", "View"); info.setTicked (view.followPlayhead); break;
        case cmd::addTrack: set ("Add audio track", "Track"); break;
        case cmd::deleteTrack: set ("Delete selected track", "Track"); info.setActive (view.selectedTrack.isNotEmpty()); break;
        case cmd::muteTrack: set ("Mute selected track", "Track"); break;
        case cmd::soloTrack: set ("Solo selected track", "Track"); break;
        case cmd::armTrack: set ("Arm selected track", "Track"); break;
        case cmd::duplicateTrack: set ("Duplicate selected track", "Track"); info.setActive (view.selectedTrack.isNotEmpty()); break;
        case cmd::toolSelect: set ("Select tool", "Tools"); info.setTicked (view.tool == Tool::Select); break;
        case cmd::toolBlade: set ("Blade tool", "Tools"); info.setTicked (view.tool == Tool::Blade); break;
        case cmd::toolAutomation: set ("Automation mode", "Tools"); info.setTicked (view.tool == Tool::Automation); break;
        case cmd::toggleSnap: set ("Snap to grid", "Tools"); info.setTicked (view.snapEnabled); break;
        case cmd::matchClipBpm: set ("Match clip to project BPM", "Clip"); info.setActive (hasClip); break;
        case cmd::matchClipKey: set ("Match clip key to project", "Clip"); info.setActive (hasClip); break;
        case cmd::reverseClip: set ("Reverse clip", "Clip"); info.setActive (hasClip); break;
        case cmd::loopClip: set ("Loop clip", "Clip"); info.setActive (hasClip); break;
        case cmd::muteClip: set ("Mute clip", "Clip"); info.setActive (hasClip); break;
        case cmd::normalizeClip: set ("Normalize clip gain", "Clip"); info.setActive (hasClip); break;
        case cmd::sliceBeats: set ("Slice by beats...", "Clip"); info.setActive (hasClip); break;
        case cmd::sliceTransients: set ("Slice at transients", "Clip"); info.setActive (hasClip); break;
        case cmd::slicePhrases: set ("Slice at phrases", "Clip"); info.setActive (hasClip); break;
        case cmd::cropToSelection: set ("Crop clip to time selection", "Clip"); info.setActive (hasClip && view.hasTimeSelection); break;
        case cmd::crossfadeSelected: set ("Crossfade selected clips", "Clip"); info.setActive (view.selectedClips.size() == 2); break;
        case cmd::mashupAssistant: set ("Mashup Assistant...", "Tools"); break;
        case cmd::separateStems: set ("Separate stems (vocals/drums/bass/other)...", "Tools"); break;
        case cmd::extractAcapella: set ("Extract acapella", "Tools"); break;
        case cmd::extractInstrumental: set ("Extract instrumental", "Tools"); break;
        case cmd::analyseSelected: set ("Re-analyse selected clip's source", "Tools"); info.setActive (hasClip); break;
        case cmd::vocalChopper: set ("Open in Vocal Chopper", "Tools"); info.setActive (hasClip); break;
        case cmd::tapTempo: set ("Tap tempo", "Tools"); break;
        case cmd::bypassEffects: set ("Bypass all effects (A/B)", "Tools"); info.setTicked (session.getAudioEngine().isBypassingAllEffects()); break;
        default: set ("?", "?"); break;
    }
}

bool Workspace::perform (const InvocationInfo& info)
{
    auto& engine = session.getAudioEngine();
    auto& transportState = engine.getTransport();
    auto& view = timeline->getView();
    auto& canvas = timeline->getCanvas();
    auto& p = session.getProject();
    auto& pc = session.getProjectController();
    auto selectedTrack = [&] { return TrackModel (p.findById (ids::TRACK, view.selectedTrack)); };
    auto firstClip = [&] { return canvas.getSelectedClips().empty() ? ClipModel() : canvas.getSelectedClips().front(); };

    switch (info.commandID)
    {
        case cmd::newProject: pc.confirmDiscardChanges ([&pc] { pc.newProject(); }); break;
        case cmd::openProject: pc.openWithDialog(); break;
        case cmd::saveProject: pc.saveWithDialog(); break;
        case cmd::saveProjectAs: pc.saveAsWithDialog(); break;
        case cmd::importAudio:
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Import audio", juce::File(), FFmpegDecoder::getSupportedWildcards());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [this, chooser] (const juce::FileChooser& fc)
            { juce::StringArray files; for (auto& f : fc.getResults()) files.add (f.getFullPathName()); if (! files.isEmpty()) importFilesAtPlayhead (files); });
            break;
        }
        case cmd::audioSettings: showAudioSettings(); break;
        case cmd::exportMix: ExportDialog::show (session, timeline->getView(), ExportDialog::What::Master); break;
        case cmd::exportStems: ExportDialog::show (session, timeline->getView(), ExportDialog::What::StemGroups); break;
        case cmd::editShortcuts: session.getKeyMap().showEditor(); break;
        case cmd::undo: session.getUndoManager().undo(); break;
        case cmd::redo: session.getUndoManager().redo(); break;
        case cmd::cut: canvas.cutSelection(); break;
        case cmd::copy: canvas.copySelection(); break;
        case cmd::paste: canvas.pasteAtPlayhead(); break;
        case cmd::deleteSelection: canvas.deleteSelected(); break;
        case cmd::selectAll: canvas.selectAll(); break;
        case cmd::duplicate: canvas.duplicateSelected(); break;
        case cmd::split: canvas.splitSelectedAtPlayhead(); break;
        case cmd::playStop: transportState.togglePlay(); break;
        case cmd::stop: if (! transportState.isPlaying()) engine.locateSeconds (0.0); transportState.stop(); break;
        case cmd::record: transportState.setRecording (! transportState.isRecording()); if (transportState.isRecording() && ! transportState.isPlaying()) transportState.play(); break;
        case cmd::goToStart: engine.locateSeconds (0.0); break;
        case cmd::toggleLoop: p.setLoop (p.getLoopStart(), p.getLoopEnd(), ! p.isLoopEnabled()); break;
        case cmd::setLoopToSelection: if (view.hasTimeSelection) p.setLoop (view.timeSelStart, view.timeSelEnd, true); break;
        case cmd::zoomIn: timeline->zoomIn(); break;
        case cmd::zoomOut: timeline->zoomOut(); break;
        case cmd::zoomToFit: timeline->zoomToFit(); break;
        case cmd::zoomToSelection: if (view.hasTimeSelection) timeline->zoomToSelection(); else timeline->zoomToFit(); break;
        case cmd::toggleBrowser: setBrowserVisible (! browserVisible); break;
        case cmd::toggleInspector: setInspectorVisible (! inspectorVisible); break;
        case cmd::toggleBottom: setBottomVisible (! bottomVisible); break;
        case cmd::showMixer: setBottomVisible (true); bottom->showTab ("Mixer"); break;
        case cmd::showChopper: setBottomVisible (true); bottom->showTab ("Vocal Chopper"); break;
        case cmd::showMashupAssistant: setBottomVisible (true); bottom->showTab ("Mashup Assistant"); break;
        case cmd::showAnalysis: setBottomVisible (true); bottom->showTab ("Analysis"); break;
        case cmd::toggleFollow: view.followPlayhead = ! view.followPlayhead; break;
        case cmd::addTrack: trackops::addTrack (p, {}); break;
        case cmd::deleteTrack: if (auto t = selectedTrack(); t.isValid()) trackops::removeTrack (p, t); break;
        case cmd::duplicateTrack: if (auto t = selectedTrack(); t.isValid()) trackops::duplicateTrack (p, t); break;
        case cmd::muteTrack: if (auto t = selectedTrack(); t.isValid()) { session.getUndoManager().beginNewTransaction ("Mute"); t.setMuted (! t.isMuted(), &session.getUndoManager()); } break;
        case cmd::soloTrack: if (auto t = selectedTrack(); t.isValid()) { session.getUndoManager().beginNewTransaction ("Solo"); t.setSolo (! t.isSolo(), &session.getUndoManager()); } break;
        case cmd::armTrack: if (auto t = selectedTrack(); t.isValid()) { session.getUndoManager().beginNewTransaction ("Arm"); t.setArmed (! t.isArmed(), &session.getUndoManager()); } break;
        case cmd::toolSelect: timeline->setTool (Tool::Select); break;
        case cmd::toolBlade: timeline->setTool (Tool::Blade); break;
        case cmd::toolAutomation:
        {
            // 'A': toggle the automation lane of the selected track (or all tracks when none is selected)
            session.getUndoManager().beginNewTransaction ("Toggle automation lanes");
            if (auto t = selectedTrack(); t.isValid()) t.setAutomationShown (! t.isAutomationShown(), &session.getUndoManager());
            else { bool any = false; for (auto tn : p.tracks()) if (TrackModel (tn).isAutomationShown()) any = true; for (auto tn : p.tracks()) TrackModel (tn).setAutomationShown (! any, &session.getUndoManager()); }
            break;
        }
        case cmd::toggleSnap: view.snapEnabled = ! view.snapEnabled; view.sendChangeMessage(); break;
        case cmd::matchClipBpm:
            for (auto c : canvas.getSelectedClips())
            {
                double src = c.getClipBpm();
                if (src <= 0) src = (double) ProjectModel::findByIdIn (p.sources(), ids::SOURCE, c.getSourceId()).getProperty (ids::bpm, 0.0);
                if (src > 0) clipops::matchToProjectBpm (p, c, src);
            }
            break;
        case cmd::matchClipKey: for (auto c : canvas.getSelectedClips()) clipops::matchToKey (p, c, p.getKeyRoot()); break;
        case cmd::reverseClip: for (auto c : canvas.getSelectedClips()) clipops::setReversed (p, c, ! c.isReversed(), p.getBpm()); break;
        case cmd::loopClip: for (auto c : canvas.getSelectedClips()) { session.getUndoManager().beginNewTransaction ("Loop clip"); if (! c.isLooped()) c.getState().setProperty (ids::sourceEnd, c.getOffset() + clipops::beatsToSourceSeconds (c, c.getLength(), p.getBpm()), &session.getUndoManager()); c.setLooped (! c.isLooped(), &session.getUndoManager()); } break;
        case cmd::muteClip: session.getUndoManager().beginNewTransaction ("Mute clips"); for (auto c : canvas.getSelectedClips()) c.setMuted (! c.isMuted(), &session.getUndoManager()); break;
        case cmd::cropToSelection: for (auto c : canvas.getSelectedClips()) clipops::cropToRange (p, c, view.timeSelStart, view.timeSelEnd, p.getBpm()); break;
        case cmd::crossfadeSelected:
        {
            auto sel = canvas.getSelectedClips();
            if (sel.size() == 2) { if (sel[0].getStart() > sel[1].getStart()) std::swap (sel[0], sel[1]); clipops::crossfade (p, sel[0], sel[1], juce::jmin (1.0, sel[0].getLength() * 0.25)); }
            break;
        }
        case cmd::sliceTransients: for (auto c : canvas.getSelectedClips()) slicer::slice (p, c, slicer::atTransients (p, c)); break;
        case cmd::slicePhrases: for (auto c : canvas.getSelectedClips()) slicer::slice (p, c, slicer::atPhrases (p, c)); break;
        case cmd::sliceBeats:
        {
            juce::PopupMenu m; m.addItem (1, "1/4 (beat)"); m.addItem (2, "1/8"); m.addItem (3, "1/16"); m.addItem (4, "Bar"); m.addItem (5, "2 bars"); m.addItem (6, "4 bars");
            m.showMenuAsync ({}, [this] (int r)
            {
                if (r <= 0) return;
                auto& proj = session.getProject(); const double bpb = proj.getTimeSigNumerator() * 4.0 / proj.getTimeSigDenominator();
                const double d[] = { 0, 1.0, 0.5, 0.25, bpb, 2 * bpb, 4 * bpb };
                for (auto c : timeline->getCanvas().getSelectedClips()) slicer::slice (proj, c, slicer::byDivision (c, d[r], proj.getBpm()));
            });
            break;
        }
        case cmd::analyseSelected: if (auto c = firstClip(); c.isValid()) session.getAnalysis().analyseSource (c.getSourceId(), true); break;
        case cmd::tapTempo: tapTempo(); break;
        case cmd::bypassEffects: engine.setBypassAllEffects (! engine.isBypassingAllEffects()); break;
        default:
            return bottom->performCommand (info.commandID);   // mixer / chopper / assistant / stems / export commands live in the bottom panel controllers
    }
    return true;
}

void Workspace::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! tapTimes.empty() && now - tapTimes.back() > 2.0) tapTimes.clear();
    tapTimes.push_back (now);
    if (tapTimes.size() >= 3)
    {
        const double bpm = 60.0 * (tapTimes.size() - 1) / (tapTimes.back() - tapTimes.front());
        if (bpm >= 40 && bpm <= 300) { session.getUndoManager().beginNewTransaction ("Tap tempo"); session.getProject().setBpm (std::round (bpm * 10.0) / 10.0); }
    }
    if (tapTimes.size() > 8) tapTimes.erase (tapTimes.begin());
}

void Workspace::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (session.getAudioEngine().getDeviceManager(), 0, 2, 2, 2, false, false, true, false);
    selector->setSize (520, 460);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector); o.dialogTitle = "Audio settings"; o.dialogBackgroundColour = colours::panelBg; o.useNativeTitleBar = true; o.resizable = false;
    settingsWindow.reset (o.create()); settingsWindow->setVisible (true);
}

// ---- menu bar -----------------------------------------------------------------------------------------------------------
juce::StringArray Workspace::getMenuBarNames() { return { "File", "Edit", "View", "Track", "Clip", "Tools", "Help" }; }

juce::PopupMenu Workspace::getMenuForIndex (int index, const juce::String&)
{
    auto& cm = session.getKeyMap().getCommandManager();
    juce::PopupMenu m;
    auto add = [&] (int id) { m.addCommandItem (&cm, id); };
    switch (index)
    {
        case 0:
            add (cmd::newProject); add (cmd::openProject);
            {
                juce::PopupMenu recent;
                if (auto* settings = session.getAppProperties().getUserSettings())
                {
                    juce::RecentlyOpenedFilesList list; list.restoreFromString (settings->getValue ("recentProjects"));
                    list.createPopupMenuItems (recent, 20000, true, true);
                }
                m.addSubMenu ("Open recent", recent);
            }
            add (cmd::saveProject); add (cmd::saveProjectAs); m.addSeparator();
            add (cmd::importAudio); add (cmd::exportMix); add (cmd::exportStems); m.addSeparator();
            add (cmd::audioSettings); add (cmd::pluginManager); add (cmd::editShortcuts); m.addSeparator();
            m.addItem (30000, "Quit");
            break;
        case 1: add (cmd::undo); add (cmd::redo); m.addSeparator(); add (cmd::cut); add (cmd::copy); add (cmd::paste); add (cmd::deleteSelection); add (cmd::duplicate); add (cmd::selectAll); m.addSeparator(); add (cmd::split); m.addSeparator(); add (cmd::toolSelect); add (cmd::toolBlade); add (cmd::toolAutomation); add (cmd::toggleSnap); break;
        case 2: add (cmd::zoomIn); add (cmd::zoomOut); add (cmd::zoomToFit); add (cmd::zoomToSelection); add (cmd::toggleFollow); m.addSeparator(); add (cmd::toggleBrowser); add (cmd::toggleInspector); add (cmd::toggleBottom); m.addSeparator(); add (cmd::showMixer); add (cmd::showChopper); add (cmd::showMashupAssistant); add (cmd::showAnalysis); break;
        case 3: add (cmd::addTrack); add (cmd::duplicateTrack); add (cmd::deleteTrack); m.addSeparator(); add (cmd::muteTrack); add (cmd::soloTrack); add (cmd::armTrack); m.addSeparator(); add (cmd::record); add (cmd::playStop); add (cmd::stop); add (cmd::goToStart); add (cmd::toggleLoop); add (cmd::setLoopToSelection); break;
        case 4: add (cmd::matchClipBpm); add (cmd::matchClipKey); m.addSeparator(); add (cmd::reverseClip); add (cmd::loopClip); add (cmd::muteClip); add (cmd::normalizeClip); add (cmd::cropToSelection); add (cmd::crossfadeSelected); m.addSeparator(); add (cmd::sliceBeats); add (cmd::sliceTransients); add (cmd::slicePhrases); m.addSeparator(); add (cmd::vocalChopper); add (cmd::analyseSelected); break;
        case 5: add (cmd::mashupAssistant); m.addSeparator(); add (cmd::separateStems); add (cmd::extractAcapella); add (cmd::extractInstrumental); m.addSeparator(); add (cmd::tapTempo); add (cmd::bypassEffects); break;
        case 6: m.addItem (30001, "About MashupDaw"); m.addItem (30002, "Open log folder"); break;
    }
    return m;
}

void Workspace::menuItemSelected (int id, int)
{
    if (id == 30000) juce::JUCEApplication::getInstance()->systemRequestedQuit();
    else if (id == 30001) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MashupDaw", "MashupDaw " JUCE_APPLICATION_VERSION_STRING "\nMashup / remix DAW.\nJUCE, FFmpeg, Rubber Band, FFTW.");
    else if (id == 30002) juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MashupDaw").revealToUser();
    else if (id >= 20000 && id < 20100)
    {
        if (auto* settings = session.getAppProperties().getUserSettings())
        {
            juce::RecentlyOpenedFilesList list; list.restoreFromString (settings->getValue ("recentProjects"));
            auto f = list.getFile (id - 20000);
            session.getProjectController().confirmDiscardChanges ([this, f] { auto r = session.getProjectController().open (f); if (r.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed", r.getErrorMessage()); });
        }
    }
}
} // namespace mashup::ui
