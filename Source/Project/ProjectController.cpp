#include "ProjectController.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectFile.h"
#include "Import/SourceLibrary.h"
#include "AudioEngine/AudioEngine.h"
#include "Waveform/WaveformCacheManager.h"
#include "Analysis/AnalysisService.h"
#include "Core/Log.h"

namespace mashup
{
ProjectController::ProjectController (Session& s) : session (s), sessionId (juce::Uuid().toDashedString().substring (0, 8))
{
    session.getProject().getRoot().addListener (this);
    startTimer (30000);   // autosave check every 30 s
}

ProjectController::~ProjectController()
{
    stopTimer();
    session.getProject().getRoot().removeListener (this);
    clearAutosave();
}

void ProjectController::markDirty()
{
    autosaveDirty = true;
    if (! dirty) { dirty = true; if (onProjectChanged) onProjectChanged(); }
}

juce::String ProjectController::getTitle() const
{
    auto f = session.getCurrentProjectFile();
    return (f == juce::File() ? juce::String ("Untitled") : f.getFileNameWithoutExtension()) + (dirty ? " *" : "") + " - MashupDaw";
}

void ProjectController::afterLoad()
{
    session.getAudioEngine().getTransport().stop();
    session.getAudioEngine().locateSeconds (0.0);
    session.getSourceLibrary().loadMissing();
    for (const auto& s : session.getProject().sources())
        if (! (bool) s.getProperty (ids::analysed, false)) session.getAnalysis().analyseSource (s[ids::id]);
    session.getAudioEngine().rebuildGraph();
    session.getAudioEngine().updateLoopFromProject();
    session.notifyProjectReplaced();
    dirty = false; autosaveDirty = false;
    if (onProjectChanged) onProjectChanged();
}

void ProjectController::newProject()
{
    clearAutosave();
    session.getAudioEngine().getTransport().stop();
    session.getSourceLibrary().clear();
    session.getWaveformCache().clear();
    session.setCurrentProjectFile ({});
    session.getProject().createDefault();
    session.getProject().setSampleRate (session.getAudioEngine().getSampleRate());
    afterLoad();
}

juce::Result ProjectController::open (const juce::File& file)
{
    juce::ValueTree root;
    session.setCurrentProjectFile (file);   // so the cache dir is right for extraction
    auto r = ProjectFile::load (file, root, session.getCacheDirectory());
    if (r.failed()) { session.setCurrentProjectFile ({}); return r; }
    clearAutosave();
    session.getAudioEngine().getTransport().stop();
    session.getSourceLibrary().clear();
    session.getWaveformCache().clear();
    session.getProject().replaceWith (root);
    afterLoad();
    if (auto* settings = session.getAppProperties().getUserSettings())
    {
        juce::RecentlyOpenedFilesList recent; recent.restoreFromString (settings->getValue ("recentProjects"));
        recent.addFile (file); recent.setMaxNumberOfItems (10);
        settings->setValue ("recentProjects", recent.toString());
    }
    return r;
}

juce::Result ProjectController::save()
{
    if (session.getCurrentProjectFile() == juce::File()) return juce::Result::fail ("untitled");
    return saveAs (session.getCurrentProjectFile());
}

juce::Result ProjectController::saveAs (const juce::File& fileIn)
{
    auto file = fileIn.hasFileExtension (ProjectFile::extension) ? fileIn : fileIn.withFileExtension (ProjectFile::extension);
    const auto oldCache = session.getCacheDirectory();
    session.getProject().setName (file.getFileNameWithoutExtension());
    session.setCurrentProjectFile (file);
    // keep waveform caches / stems / recordings with the project when it is saved somewhere new
    if (auto newCache = session.getCacheDirectory(); oldCache.isDirectory() && newCache != oldCache)
    {
        newCache.createDirectory();
        for (const auto& sub : { "waveforms", "stems", "recordings" })
        {
            auto from = oldCache.getChildFile (sub);
            if (from.isDirectory() && ! newCache.getChildFile (sub).exists()) from.copyDirectoryTo (newCache.getChildFile (sub));
        }
    }
    auto r = ProjectFile::save (session.getProject().getRoot(), file, session.getCacheDirectory());
    if (r.wasOk())
    {
        dirty = false; autosaveDirty = false; clearAutosave();
        if (auto* settings = session.getAppProperties().getUserSettings())
        {
            juce::RecentlyOpenedFilesList recent; recent.restoreFromString (settings->getValue ("recentProjects"));
            recent.addFile (file); recent.setMaxNumberOfItems (10);
            settings->setValue ("recentProjects", recent.toString());
        }
        if (onProjectChanged) onProjectChanged();
    }
    return r;
}

void ProjectController::openWithDialog()
{
    confirmDiscardChanges ([this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Open project", juce::File(), juce::String ("*") + ProjectFile::extension);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            auto r = open (fc.getResult());
            if (r.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed", r.getErrorMessage());
        });
    });
}

void ProjectController::saveWithDialog (std::function<void()> then)
{
    if (session.getCurrentProjectFile() == juce::File()) { saveAsWithDialog (std::move (then)); return; }
    auto r = save();
    if (r.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", r.getErrorMessage());
    else if (then) then();
}

void ProjectController::saveAsWithDialog (std::function<void()> then)
{
    auto start = session.getCurrentProjectFile() != juce::File() ? session.getCurrentProjectFile() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (session.getProject().getName() + ProjectFile::extension);
    auto chooser = std::make_shared<juce::FileChooser> ("Save project", start, juce::String ("*") + ProjectFile::extension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, chooser, then] (const juce::FileChooser& fc)
    {
        if (fc.getResult() == juce::File()) return;
        auto r = saveAs (fc.getResult());
        if (r.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", r.getErrorMessage());
        else if (then) then();
    });
}

void ProjectController::confirmDiscardChanges (std::function<void()> proceed)
{
    if (! dirty) { proceed(); return; }
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, "Unsaved changes", "Save changes to \"" + session.getProject().getName() + "\"?", "Save", "Don't save", "Cancel", nullptr,
        juce::ModalCallbackFunction::create ([this, proceed] (int r) { if (r == 1) saveWithDialog (proceed); else if (r == 2) proceed(); }));
}

// ---- autosave ---------------------------------------------------------------------------------------------------------
juce::File ProjectController::getAutosaveDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MashupDaw").getChildFile ("autosave");
}

juce::File ProjectController::autosaveFileForCurrent() const { return getAutosaveDirectory().getChildFile ("session-" + sessionId + ".mashup.autosave"); }

void ProjectController::autosaveNow()
{
    auto dir = getAutosaveDirectory(); dir.createDirectory();
    auto root = session.getProject().getRoot().createCopy();
    root.setProperty ("originalFile", session.getCurrentProjectFile().getFullPathName(), nullptr);
    root.setProperty ("autosaveTime", juce::Time::getCurrentTime().toISO8601 (true), nullptr);
    auto r = ProjectFile::saveXml (root, autosaveFileForCurrent());
    if (r.failed()) log ("Autosave failed: " + r.getErrorMessage()); else autosaveDirty = false;
}

void ProjectController::timerCallback() { if (autosaveDirty) autosaveNow(); }

juce::Array<juce::File> ProjectController::findRecoverableAutosaves()
{
    juce::Array<juce::File> out;
    for (const auto& f : getAutosaveDirectory().findChildFiles (juce::File::findFiles, false, "*.mashup.autosave")) out.add (f);
    return out;
}

juce::Result ProjectController::recoverFrom (const juce::File& autosave)
{
    juce::ValueTree root;
    auto r = ProjectFile::loadXml (autosave, root);
    if (r.failed()) return r;
    juce::File original (root["originalFile"].toString());
    root.removeProperty ("originalFile", nullptr); root.removeProperty ("autosaveTime", nullptr);
    ProjectFile::resolveSourcePaths (root, original != juce::File() ? original : autosave);
    session.getAudioEngine().getTransport().stop();
    session.getSourceLibrary().clear(); session.getWaveformCache().clear();
    session.setCurrentProjectFile (original.existsAsFile() ? original : juce::File());
    session.getProject().replaceWith (root);
    afterLoad();
    dirty = true; if (onProjectChanged) onProjectChanged();
    autosave.deleteFile();
    return juce::Result::ok();
}

void ProjectController::clearAutosave() { autosaveFileForCurrent().deleteFile(); }
} // namespace mashup
