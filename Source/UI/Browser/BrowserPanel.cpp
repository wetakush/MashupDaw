#include "BrowserPanel.h"
#include "UI/Theme/Theme.h"
#include "Import/FFmpegDecoder.h"
#include "Import/SourceLibrary.h"
#include "AudioEngine/AudioEngine.h"
#include "Core/MusicalKey.h"
#include "Core/MusicalTime.h"
#include "Analysis/AnalysisService.h"
#include "Waveform/WaveformCacheManager.h"

namespace mashup::ui
{
namespace
{
    class FileTreeWithDrag : public juce::FileTreeComponent
    {
    public:
        using FileTreeComponent::FileTreeComponent;
        juce::var getDragSourceDescription (const juce::SparseSet<int>&) { return {}; }
    };
}

BrowserPanel::BrowserPanel (Session& s)
    : session (s), filter (FFmpegDecoder::getSupportedWildcards(), "*", "audio files")
{
    scanThread.startThread (juce::Thread::Priority::background);
    contents = std::make_unique<juce::DirectoryContentsList> (&filter, scanThread);
    fileTree = std::make_unique<juce::FileTreeComponent> (*contents);
    fileTree->addListener (this);
    fileTree->setDragAndDropDescription ("file:");   // replaced per-file in fileClicked
    fileTree->setItemHeight (20);
    fileTree->setColour (juce::TreeView::backgroundColourId, colours::panelBg);

    filesTab.addAndMakeVisible (*fileTree);
    for (auto* b : { &homeButton, &musicButton, &upButton }) filesTab.addAndMakeVisible (*b);
    filesTab.addAndMakeVisible (pathLabel);
    pathLabel.setFont (Theme::ui (10.0f)); pathLabel.setColour (juce::Label::textColourId, colours::textDim);
    homeButton.onClick = [this] { setRoot (juce::File::getSpecialLocation (juce::File::userHomeDirectory)); };
    musicButton.onClick = [this] { setRoot (juce::File::getSpecialLocation (juce::File::userMusicDirectory)); };
    upButton.onClick = [this] { setRoot (contents->getDirectory().getParentDirectory()); };

    sourcesList.setModel (&sourcesModel);
    sourcesList.setRowHeight (36);
    sourcesList.setColour (juce::ListBox::backgroundColourId, colours::panelBg);
    sourcesTab.addAndMakeVisible (sourcesList);

    tabs.setTabBarDepth (24); tabs.setOutline (0);
    tabs.addTab ("Files", colours::panelBg, &filesTab, false);
    tabs.addTab ("Sources", colours::panelBg, &sourcesTab, false);
    addAndMakeVisible (tabs);

    addAndMakeVisible (stopAudition); addAndMakeVisible (auditionLabel);
    auditionLabel.setFont (Theme::ui (11.0f)); auditionLabel.setColour (juce::Label::textColourId, colours::textDim);
    stopAudition.onClick = [this] { session.getAudioEngine().stopAudition(); };

    juce::File start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (auto* props = session.getAppProperties().getUserSettings()) { auto p = props->getValue ("browserRoot"); if (p.isNotEmpty() && juce::File (p).isDirectory()) start = juce::File (p); }
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    setRoot (start);

    session.getProject().getRoot().addListener (this);
    session.getAnalysis().addChangeListener (this);
    session.getWaveformCache().addChangeListener (this);
    startTimerHz (10);
}

BrowserPanel::~BrowserPanel()
{
    session.getProject().getRoot().removeListener (this);
    session.getAnalysis().removeChangeListener (this);
    session.getWaveformCache().removeChangeListener (this);
    fileTree->removeListener (this);
    fileTree.reset(); contents.reset();
    scanThread.stopThread (2000);
}

void BrowserPanel::setRoot (const juce::File& dir)
{
    if (! dir.isDirectory()) return;
    contents->setDirectory (dir, true, true);
    pathLabel.setText (dir.getFullPathName(), juce::dontSendNotification);
    if (auto* props = session.getAppProperties().getUserSettings()) props->setValue ("browserRoot", dir.getFullPathName());
}

void BrowserPanel::fileClicked (const juce::File& f, const juce::MouseEvent& e)
{
    if (f.isDirectory()) { if (e.getNumberOfClicks() >= 2) setRoot (f); return; }
    fileTree->setDragAndDropDescription ("file:" + f.getFullPathName());
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m; m.addItem (1, "Import to new track"); m.addItem (2, "Audition"); m.addItem (3, "Analyse (BPM / key)");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, f] (int r)
        {
            if (r == 1) { if (auto* top = findParentComponentOfClass<juce::Component>()) { juce::StringArray files (f.getFullPathName()); if (onImportFiles) onImportFiles (files); } }
            else if (r == 2) fileDoubleClicked (f);
            else if (r == 3) { auto id = session.getSourceLibrary().importFile (f); session.getAnalysis().analyseSource (id); tabs.setCurrentTabIndex (1); }
        });
    }
}

void BrowserPanel::fileDoubleClicked (const juce::File& f)
{
    if (f.isDirectory()) { setRoot (f); return; }
    // audition: decode (cached in the library as a source) and play
    auditionFile = f;
    auditionId = session.getSourceLibrary().importFile (f);
    auditionLabel.setText ("Loading " + f.getFileName() + "...", juce::dontSendNotification);
}

void BrowserPanel::timerCallback()
{
    auto& engine = session.getAudioEngine();
    if (auditionId.isNotEmpty())
    {
        if (auto src = session.getSourceLibrary().get (auditionId))
        {
            engine.startAudition (src, 0.0, 0.8);
            auditionLabel.setText ("> " + auditionFile.getFileName(), juce::dontSendNotification);
            auditionId.clear();
        }
        else if (! session.getSourceLibrary().isLoading (auditionId)) { auditionLabel.setText ("Could not load " + auditionFile.getFileName(), juce::dontSendNotification); auditionId.clear(); }
    }
    else if (engine.isAuditioning()) auditionLabel.setText ("> " + auditionFile.getFileName() + "  " + formatDurationShort (engine.getAuditionPositionSeconds()), juce::dontSendNotification);
    else if (auditionLabel.getText().startsWith (">")) auditionLabel.setText ("", juce::dontSendNotification);
}

void BrowserPanel::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); g.setColour (colours::border); g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight()); }

void BrowserPanel::resized()
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (24).reduced (4, 2);
    stopAudition.setBounds (bottom.removeFromRight (26)); auditionLabel.setBounds (bottom);
    tabs.setBounds (r);
    auto ft = filesTab.getLocalBounds();
    auto row = ft.removeFromTop (24).reduced (4, 2);
    homeButton.setBounds (row.removeFromLeft (46)); row.removeFromLeft (3); musicButton.setBounds (row.removeFromLeft (46)); row.removeFromLeft (3); upButton.setBounds (row.removeFromLeft (36));
    pathLabel.setBounds (ft.removeFromTop (16).reduced (4, 0));
    fileTree->setBounds (ft);
    sourcesList.setBounds (sourcesTab.getLocalBounds());
}

// ---- sources list -----------------------------------------------------------------------------------------------------
int BrowserPanel::SourcesModel::getNumRows() { return owner.session.getProject().sources().getNumChildren(); }

void BrowserPanel::SourcesModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    auto node = owner.session.getProject().sources().getChild (row);
    if (! node.isValid()) return;
    g.fillAll (selected ? colours::accentDim.withAlpha (0.3f) : (row % 2 ? colours::panelBg : colours::panelBgAlt));
    g.setColour (colours::text); g.setFont (Theme::ui (12.0f, true));
    juce::String name = node[ids::name].toString();
    if (node.hasProperty (ids::stemType)) name += "  [" + node[ids::stemType].toString() + "]";
    g.drawText (name, 8, 2, w - 16, 16, juce::Justification::centredLeft, true);
    g.setFont (Theme::mono (10.0f)); g.setColour (colours::textDim);
    juce::String info;
    const double sr = (double) node[ids::sampleRate]; const double len = sr > 0 ? (double) node[ids::lengthSamples] / sr : 0.0;
    info << formatDurationShort (len) << "  " << juce::String ((int) sr) << "Hz " << (int) node[ids::channels] << "ch";
    auto& an = owner.session.getAnalysis();
    if (an.isAnalysing (node[ids::id])) info << "   analysing " << (int) (an.getProgress (node[ids::id]) * 100) << "%";
    else if ((bool) node.getProperty (ids::analysed, false))
        info << "   " << juce::String ((double) node[ids::bpm], 1) << " BPM  " << key::name ((int) node[ids::keyRoot], (int) node[ids::keyMode]) << " (" << key::camelot ((int) node[ids::keyRoot], (int) node[ids::keyMode]) << ")  " << juce::String ((double) node[ids::lufs], 1) << " LUFS";
    else if (owner.session.getSourceLibrary().isLoading (node[ids::id])) info << "   loading " << (int) (owner.session.getSourceLibrary().getProgress (node[ids::id]) * 100) << "%";
    g.drawText (info, 8, 18, w - 16, 14, juce::Justification::centredLeft, true);
}

juce::var BrowserPanel::SourcesModel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.size() == 0) return {};
    auto node = owner.session.getProject().sources().getChild (rows[0]);
    return node.isValid() ? juce::var ("source:" + node[ids::id].toString()) : juce::var();
}

void BrowserPanel::SourcesModel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    auto node = owner.session.getProject().sources().getChild (row);
    if (! node.isValid() || ! e.mods.isPopupMenu()) return;
    juce::PopupMenu m;
    m.addItem (1, "Audition"); m.addItem (2, "Re-analyse"); m.addItem (3, "Place on new track at playhead"); m.addSeparator(); m.addItem (4, "Separate stems..."); m.addSeparator(); m.addItem (5, "Remove from project (keeps clips' references)");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, node] (int r)
    {
        const juce::String id = node[ids::id];
        if (r == 1) { if (auto src = owner.session.getSourceLibrary().get (id)) owner.session.getAudioEngine().startAudition (src, 0.0, 0.8); }
        else if (r == 2) owner.session.getAnalysis().analyseSource (id, true);
        else if (r == 3 && owner.onPlaceSource) owner.onPlaceSource (id);
        else if (r == 4 && owner.onSeparateStems) owner.onSeparateStems (id);
        else if (r == 5) { auto& p = owner.session.getProject(); p.getUndoManager().beginNewTransaction ("Remove source"); p.sources().removeChild (node, &p.getUndoManager()); }
    });
}

void BrowserPanel::SourcesModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    auto node = owner.session.getProject().sources().getChild (row);
    if (auto src = owner.session.getSourceLibrary().get (node[ids::id])) owner.session.getAudioEngine().startAudition (src, 0.0, 0.8);
}
} // namespace mashup::ui
