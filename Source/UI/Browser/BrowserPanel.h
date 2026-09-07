#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"

namespace mashup::ui
{
/** Left panel: file browser (drag files to the timeline, click to audition) and project sources/stems list. */
class BrowserPanel : public juce::Component, public juce::DragAndDropContainer,
                     private juce::FileBrowserListener, private juce::ValueTree::Listener, private juce::ChangeListener, private juce::Timer
{
public:
    explicit BrowserPanel (Session&);
    ~BrowserPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setRoot (const juce::File&);

    std::function<void (const juce::StringArray&)> onImportFiles;
    std::function<void (const juce::String& sourceId)> onPlaceSource;
    std::function<void (const juce::String& sourceId)> onSeparateStems;

private:
    // file browser
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override;
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sourcesList.updateContent(); sourcesList.repaint(); }
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (ids::SOURCES)) { sourcesList.updateContent(); } }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (ids::SOURCES)) sourcesList.updateContent(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) override { if (v.hasType (ids::SOURCE)) sourcesList.repaint(); }
    void valueTreeRedirected (juce::ValueTree&) override { sourcesList.updateContent(); }
    void timerCallback() override;

    struct SourcesModel : public juce::ListBoxModel
    {
        SourcesModel (BrowserPanel& o) : owner (o) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        BrowserPanel& owner;
    };

    Session& session;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TimeSliceThread scanThread { "file browser" };
    juce::WildcardFileFilter filter;
    std::unique_ptr<juce::DirectoryContentsList> contents;
    std::unique_ptr<juce::FileTreeComponent> fileTree;
    juce::Component filesTab, sourcesTab;
    juce::TextButton homeButton { "Home" }, musicButton { "Music" }, upButton { "Up" }, stopAudition { "Stop" };
    juce::Label pathLabel;
    SourcesModel sourcesModel { *this };
    juce::ListBox sourcesList;
    juce::File auditionFile; juce::String auditionId;
    juce::Label auditionLabel;
};
}
