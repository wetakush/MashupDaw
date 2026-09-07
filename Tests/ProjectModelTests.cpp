#include <juce_core/juce_core.h>
#include "Project/ProjectModel.h"

using namespace mashup;

class ProjectModelTests : public juce::UnitTest
{
public:
    ProjectModelTests() : juce::UnitTest ("ProjectModel", "Project") {}
    void runTest() override
    {
        beginTest ("default project has sections and undoable bpm");
        juce::UndoManager um;
        ProjectModel m (um);
        expect (m.tracks().isValid());
        expect (m.sources().isValid());
        expect (m.buses().getNumChildren() == 2);
        um.beginNewTransaction();
        m.setBpm (128.0);
        expectWithinAbsoluteError (m.getBpm(), 128.0, 1e-9);
        expect (um.undo());
        expectWithinAbsoluteError (m.getBpm(), 120.0, 1e-9);
        expect (um.redo());
        expectWithinAbsoluteError (m.getBpm(), 128.0, 1e-9);

        beginTest ("ids are unique");
        juce::StringArray ids;
        for (int i = 0; i < 500; ++i) ids.add (ProjectModel::newId());
        ids.removeDuplicates (false);
        expectEquals (ids.size(), 500);
    }
};
static ProjectModelTests projectModelTests;
