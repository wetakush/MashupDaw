#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"

namespace mashup::ui
{
class Workspace;

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, Session&);
    ~MainWindow() override;
    void closeButtonPressed() override;
    Workspace& getWorkspace();
private:
    Session& session;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
}
