#include "MainWindow.h"
#include "Workspace.h"
#include "UI/Theme/Theme.h"
#include "Project/ProjectController.h"

namespace mashup::ui
{
MainWindow::MainWindow (const juce::String& name, Session& s)
    : DocumentWindow (name, colours::windowBg, DocumentWindow::allButtons), session (s)
{
    setUsingNativeTitleBar (true);
    auto* ws = new Workspace (session);
    setContentOwned (ws, true);
    if (std::getenv ("MASHUP_SKIP") == nullptr || ! juce::String (std::getenv ("MASHUP_SKIP")).contains ("menubar")) setMenuBar (ws, 26);
    setResizable (true, false);
    setResizeLimits (1024, 640, 10000, 10000);
    centreWithSize (1600, 960);
    session.getProjectController().onProjectChanged = [this] { setName (session.getProjectController().getTitle()); };
    setName (session.getProjectController().getTitle());
    setVisible (true);
}

MainWindow::~MainWindow() { session.getProjectController().onProjectChanged = nullptr; setMenuBar (nullptr); }

Workspace& MainWindow::getWorkspace() { return *dynamic_cast<Workspace*> (getContentComponent()); }

void MainWindow::closeButtonPressed()
{
    session.getProjectController().confirmDiscardChanges ([] { juce::JUCEApplication::getInstance()->quit(); });
}
}
