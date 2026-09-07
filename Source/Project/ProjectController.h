#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"

namespace mashup
{
/** New / open / save / autosave / crash recovery. Message thread. */
class ProjectController : private juce::Timer, private juce::ValueTree::Listener
{
public:
    explicit ProjectController (Session&);
    ~ProjectController() override;

    void newProject();
    juce::Result open (const juce::File&);
    juce::Result save();                      // to the current file (fails if untitled)
    juce::Result saveAs (const juce::File&);
    bool isDirty() const noexcept { return dirty; }
    juce::String getTitle() const;

    /** Async UI flows (file choosers, confirmation). `then` runs after a successful operation. */
    void openWithDialog();
    void saveWithDialog (std::function<void()> then = {});
    void saveAsWithDialog (std::function<void()> then = {});
    /** Asks to save if dirty, then calls `proceed` (unless cancelled). */
    void confirmDiscardChanges (std::function<void()> proceed);

    // autosave / crash recovery
    void autosaveNow();
    static juce::File getAutosaveDirectory();
    /** Returns autosave files left behind by a crashed session. */
    static juce::Array<juce::File> findRecoverableAutosaves();
    juce::Result recoverFrom (const juce::File& autosave);
    void clearAutosave();

    std::function<void()> onProjectChanged;   // title, dirty state or file changed

private:
    void timerCallback() override;
    void markDirty();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override { if (p != ids::zoom && p != ids::scroll && p != ids::playhead && p != ids::lengthSamples && p != ids::analysed) markDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { markDirty(); }
    void valueTreeRedirected (juce::ValueTree&) override { markDirty(); }
    juce::File autosaveFileForCurrent() const;
    void afterLoad();

    Session& session;
    bool dirty = false, autosaveDirty = false;
    juce::String sessionId;
};
} // namespace mashup
