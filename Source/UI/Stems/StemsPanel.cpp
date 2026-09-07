#include "StemsPanel.h"
#include "UI/Theme/Theme.h"
#include "Clips/ClipModel.h"

namespace mashup::ui
{
StemsPanel::StemsPanel (Session& s, TimelineViewState& v) : session (s), view (v)
{
    addAndMakeVisible (title); title.setFont (Theme::ui (13.0f, true)); title.setText ("Stem separation (Demucs)", juce::dontSendNotification);
    addAndMakeVisible (status); status.setFont (Theme::ui (11.0f)); status.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (modelLabel); modelLabel.setText ("Model", juce::dontSendNotification); modelLabel.setFont (Theme::ui (11.0f)); modelLabel.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (deviceLabel); deviceLabel.setText ("Device", juce::dontSendNotification); deviceLabel.setFont (Theme::ui (11.0f)); deviceLabel.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (modelBox);
    modelBox.addItem ("htdemucs (fast, good)", 1); modelBox.addItem ("htdemucs_ft (fine-tuned, 4x slower)", 2); modelBox.addItem ("htdemucs_6s (6 stems: +piano, guitar)", 3); modelBox.addItem ("mdx_extra (MDX, high quality)", 4);
    modelBox.setSelectedId (1);
    addAndMakeVisible (deviceBox);
    deviceBox.addItem ("Auto (CUDA if available)", 1); deviceBox.addItem ("CUDA", 2); deviceBox.addItem ("CPU", 3); deviceBox.setSelectedId (1);
    for (auto* b : { &fourStems, &acapella, &instrumental, &clearButton }) addAndMakeVisible (*b);
    fourStems.onClick = [this] { startForSelection (StemSeparationService::Mode::FourStems); };
    acapella.onClick = [this] { startForSelection (StemSeparationService::Mode::Acapella); };
    instrumental.onClick = [this] { startForSelection (StemSeparationService::Mode::Instrumental); };
    clearButton.onClick = [this] { session.getStemSeparation().clearFinished(); };
    addAndMakeVisible (muteOriginal); muteOriginal.setToggleState (true, juce::dontSendNotification);
    muteOriginal.onClick = [this] { session.getStemSeparation().muteOriginalAfterSeparation = muteOriginal.getToggleState(); };
    addAndMakeVisible (jobList); jobList.setModel (this); jobList.setRowHeight (30);
    session.getStemSeparation().addChangeListener (this);
    view.addChangeListener (this);
    refreshStatus();
    startTimerHz (5);
}
StemsPanel::~StemsPanel() { session.getStemSeparation().removeChangeListener (this); view.removeChangeListener (this); }

juce::String StemsPanel::selectedSourceId() const
{
    if (view.selectedClips.empty()) return {};
    return ClipModel (session.getProject().findById (ids::CLIP, *view.selectedClips.begin())).getSourceId();
}

void StemsPanel::refreshStatus()
{
    auto& svc = session.getStemSeparation();
    const auto src = selectedSourceId();
    auto node = ProjectModel::findByIdIn (session.getProject().sources(), ids::SOURCE, src);
    status.setText (svc.getEnvironmentStatus() + (node.isValid() ? "   |   selected: " + node[ids::name].toString() : "   |   select a clip on the timeline"), juce::dontSendNotification);
    const bool enable = node.isValid();
    fourStems.setEnabled (enable); acapella.setEnabled (enable); instrumental.setEnabled (enable);
}

void StemsPanel::startForSelection (StemSeparationService::Mode mode)
{
    const auto src = selectedSourceId();
    if (src.isEmpty()) return;
    static const char* models[] = { "htdemucs", "htdemucs_ft", "htdemucs_6s", "mdx_extra" };
    static const char* devices[] = { "auto", "cuda", "cpu" };
    session.getStemSeparation().separate (src, mode, models[juce::jlimit (0, 3, modelBox.getSelectedId() - 1)], devices[juce::jlimit (0, 2, deviceBox.getSelectedId() - 1)]);
}

int StemsPanel::getNumRows() { return (int) session.getStemSeparation().getJobs().size(); }

void StemsPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    const auto& jobs = session.getStemSeparation().getJobs();
    if (row >= (int) jobs.size()) return;
    const auto& j = jobs[(size_t) row];
    g.fillAll (row % 2 ? colours::panelBg : colours::panelBgAlt);
    g.setColour (colours::text); g.setFont (Theme::ui (12.0f, true));
    g.drawText (j.sourceName + "  [" + StemSeparationService::modeName (j.mode) + ", " + j.model + "]", 8, 2, w / 2, 14, juce::Justification::centredLeft, true);
    juce::String st;
    switch (j.state)
    {
        case StemSeparationService::Job::State::Queued: st = "queued"; break;
        case StemSeparationService::Job::State::Running: st = j.stage + "  " + juce::String ((int) (j.progress * 100)) + "%" + (j.usedDevice.isNotEmpty() ? "  on " + j.usedDevice : ""); break;
        case StemSeparationService::Job::State::Done: st = "done: " + juce::String ((int) j.files.size()) + " stems added to the timeline"; break;
        case StemSeparationService::Job::State::Failed: st = "failed: " + j.error.upToFirstOccurrenceOf ("\n", false, false); break;
        case StemSeparationService::Job::State::Cancelled: st = "cancelled"; break;
    }
    g.setFont (Theme::ui (11.0f)); g.setColour (j.state == StemSeparationService::Job::State::Failed ? colours::danger : colours::textDim);
    g.drawText (st, 8, 16, w - 120, 12, juce::Justification::centredLeft, true);
    // progress bar
    auto bar = juce::Rectangle<int> (w / 2 + 8, 6, w / 2 - 110, 8);
    g.setColour (colours::windowBg); g.fillRect (bar);
    g.setColour (j.state == StemSeparationService::Job::State::Done ? colours::meterLow : colours::accent);
    g.fillRect (bar.withWidth ((int) (bar.getWidth() * juce::jlimit (0.0f, 1.0f, j.progress))));
    if (j.state == StemSeparationService::Job::State::Queued || j.state == StemSeparationService::Job::State::Running)
    {
        g.setColour (colours::headerBg); g.fillRoundedRectangle (juce::Rectangle<float> ((float) w - 90, 5.0f, 80.0f, 20.0f), 3.0f);
        g.setColour (colours::text); g.drawText ("Cancel", w - 90, 5, 80, 20, juce::Justification::centred);
    }
}

void StemsPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    const auto& jobs = session.getStemSeparation().getJobs();
    if (row >= (int) jobs.size()) return;
    if (e.x > getWidth() - 100) session.getStemSeparation().cancel (jobs[(size_t) row].id);
}

void StemsPanel::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); }

void StemsPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto left = r.removeFromLeft (juce::jmin (420, r.getWidth() / 2));
    title.setBounds (left.removeFromTop (20));
    status.setBounds (left.removeFromTop (18));
    { auto row = left.removeFromTop (26); modelLabel.setBounds (row.removeFromLeft (44)); modelBox.setBounds (row.reduced (0, 2)); }
    { auto row = left.removeFromTop (26); deviceLabel.setBounds (row.removeFromLeft (44)); deviceBox.setBounds (row.removeFromLeft (200).reduced (0, 2)); }
    fourStems.setBounds (left.removeFromTop (26).reduced (0, 2));
    { auto row = left.removeFromTop (26); acapella.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (1)); instrumental.setBounds (row.reduced (1)); }
    muteOriginal.setBounds (left.removeFromTop (22));
    r.removeFromLeft (8);
    clearButton.setBounds (r.removeFromTop (22).removeFromRight (110));
    jobList.setBounds (r);
}
} // namespace mashup::ui
