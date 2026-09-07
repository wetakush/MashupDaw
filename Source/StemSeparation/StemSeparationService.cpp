#include "StemSeparationService.h"
#include "Project/ProjectModel.h"
#include "Core/Log.h"

namespace mashup
{
StemSeparationService::StemSeparationService (ProjectModel& p, SourceLibrary& s, std::function<juce::File()> dir)
    : Thread ("stem separation"), project (p), sources (s), cacheDir (std::move (dir)) {}

StemSeparationService::~StemSeparationService()
{
    signalThreadShouldExit();
    if (activeProcess) activeProcess->kill();
    stopThread (5000);
    cancelPendingUpdate();
}

juce::File StemSeparationService::getWorkerScript() const { return juce::File (MASHUP_SCRIPTS_DIR).getChildFile ("demucs_worker.py"); }

juce::File StemSeparationService::getPythonExecutable() const
{
    if (const char* env = std::getenv ("MASHUP_STEMS_VENV")) { juce::File f = juce::File (env).getChildFile ("bin/python"); if (f.existsAsFile()) return f; }
    juce::File repoVenv = juce::File (MASHUP_SCRIPTS_DIR).getParentDirectory().getChildFile (".venv-stems/bin/python");
    if (repoVenv.existsAsFile()) return repoVenv;
    juce::File appVenv = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MashupDaw/venv-stems/bin/python");
    if (appVenv.existsAsFile()) return appVenv;
    return juce::File ("/usr/bin/python3");
}

juce::String StemSeparationService::getEnvironmentStatus() const
{
    auto py = getPythonExecutable();
    if (! py.existsAsFile()) return "Python not found. Run Scripts/setup_stems_env.sh";
    if (! getWorkerScript().existsAsFile()) return "Worker script missing: " + getWorkerScript().getFullPathName();
    if (! py.getFullPathName().contains ("venv-stems")) return "Using system python (" + py.getFullPathName() + "); run Scripts/setup_stems_env.sh to install Demucs";
    return "Ready: " + py.getFullPathName();
}

StemSeparationService::Job* StemSeparationService::findJob (int id) { for (auto& j : jobs) if (j.id == id) return &j; return nullptr; }

int StemSeparationService::separate (const juce::String& sourceId, Mode mode, const juce::String& model, const juce::String& device)
{
    auto node = ProjectModel::findByIdIn (project.sources(), ids::SOURCE, sourceId);
    if (! node.isValid()) return -1;
    Job j; j.id = nextId++; j.sourceId = sourceId; j.sourceName = node[ids::name].toString();
    j.inputFile = juce::File (node[ids::path].toString());
    j.outputDir = cacheDir().getChildFile ("stems").getChildFile (juce::File::createLegalFileName (j.sourceName + "_" + sourceId.substring (0, 6)));
    j.mode = mode; j.model = model; j.device = device;
    jobs.push_back (j);
    { const juce::ScopedLock l (lock); queue.push_back (j.id); }
    if (! isThreadRunning()) startThread();
    sendChangeMessage();
    return j.id;
}

void StemSeparationService::cancel (int jobId)
{
    { const juce::ScopedLock l (lock); queue.erase (std::remove (queue.begin(), queue.end(), jobId), queue.end()); }
    if (auto* j = findJob (jobId))
    {
        if (j->state == Job::State::Queued) { j->state = Job::State::Cancelled; sendChangeMessage(); }
        else if (j->state == Job::State::Running) { cancelRequested.store (jobId); if (activeProcess && activeJobId.load() == jobId) activeProcess->kill(); }
    }
}

void StemSeparationService::clearFinished()
{
    jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [] (const Job& j) { return j.state == Job::State::Done || j.state == Job::State::Failed || j.state == Job::State::Cancelled; }), jobs.end());
    sendChangeMessage();
}

void StemSeparationService::postUpdate (int jobId, std::function<void (Job&)> update)
{
    { const juce::ScopedLock l (lock); pendingUpdates.push_back ({ jobId, std::move (update) }); }
    triggerAsyncUpdate();
}

void StemSeparationService::handleAsyncUpdate()
{
    std::vector<std::pair<int, std::function<void (Job&)>>> updates;
    { const juce::ScopedLock l (lock); updates.swap (pendingUpdates); }
    for (auto& [id, fn] : updates)
        if (auto* j = findJob (id))
        {
            fn (*j);
            if (j->state == Job::State::Done && onJobFinished) onJobFinished (*j);
        }
    sendChangeMessage();
}

void StemSeparationService::run()
{
    while (! threadShouldExit())
    {
        int id = -1;
        Job copy;
        {
            const juce::ScopedLock l (lock);
            if (queue.empty()) break;
            id = queue.front(); queue.pop_front();
        }
        // fetch a copy of the job (jobs vector is message-thread owned, but ids never move; read under lock)
        {
            juce::WaitableEvent done;
            bool found = false;
            juce::MessageManager::callAsync ([this, id, &copy, &found, &done] { if (auto* j = findJob (id)) { copy = *j; found = true; } done.signal(); });
            done.wait (3000);
            if (! found) continue;
        }
        activeJobId.store (id);
        processJob (copy);
        activeJobId.store (-1);
    }
}

void StemSeparationService::processJob (Job job)
{
    const int id = job.id;
    postUpdate (id, [] (Job& j) { j.state = Job::State::Running; j.stage = "starting"; j.progress = 0.0f; });
    if (! job.inputFile.existsAsFile()) { postUpdate (id, [f = job.inputFile] (Job& j) { j.state = Job::State::Failed; j.error = "input file missing: " + f.getFullPathName(); }); return; }
    job.outputDir.createDirectory();

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("input", job.inputFile.getFullPathName());
    obj->setProperty ("output_dir", job.outputDir.getFullPathName());
    obj->setProperty ("model", job.model);
    obj->setProperty ("device", job.device);
    obj->setProperty ("name", job.sourceName);
    obj->setProperty ("two_stems", job.mode == Mode::FourStems ? juce::var() : juce::var ("vocals"));
    obj->setProperty ("shifts", 1);
    obj->setProperty ("overlap", 0.25);
    auto jobFile = job.outputDir.getChildFile ("job.json");
    jobFile.replaceWithText (juce::JSON::toString (juce::var (obj.get())));

    juce::StringArray args { getPythonExecutable().getFullPathName(), getWorkerScript().getFullPathName(), "--job", jobFile.getFullPathName() };
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        postUpdate (id, [] (Job& j) { j.state = Job::State::Failed; j.error = "could not start python worker"; });
        return;
    }
    activeProcess = std::move (proc);
    juce::String buffer, stderrTail;
    std::map<juce::String, juce::File> files;
    bool done = false, failed = false; juce::String error, usedDevice;
    char chunk[4096];
    while (! threadShouldExit())
    {
        const int n = activeProcess->readProcessOutput (chunk, sizeof (chunk));
        if (n <= 0)
        {
            if (! activeProcess->isRunning()) break;
            juce::Thread::sleep (20);
            continue;
        }
        buffer += juce::String::fromUTF8 (chunk, n);
        int nl;
        while ((nl = buffer.indexOfChar ('\n')) >= 0)
        {
            auto line = buffer.substring (0, nl).trim(); buffer = buffer.substring (nl + 1);
            if (line.isEmpty()) continue;
            auto v = juce::JSON::parse (line);
            if (! v.isObject()) { stderrTail = (stderrTail + "\n" + line).getLastCharacters (2000); continue; }   // stderr noise (torch warnings)
            const auto type = v["type"].toString();
            if (type == "progress") { const float p = (float) (double) v["value"]; const juce::String stage = v["stage"].toString(); postUpdate (id, [p, stage] (Job& j) { j.progress = p; j.stage = stage; }); }
            else if (type == "info") { if (v.hasProperty ("device")) usedDevice = v["device"].toString() + (v["gpu"].toString().isNotEmpty() ? " (" + v["gpu"].toString() + ")" : ""); postUpdate (id, [d = usedDevice] (Job& j) { if (d.isNotEmpty()) j.usedDevice = d; }); }
            else if (type == "done")
            {
                if (auto* fo = v["files"].getDynamicObject())
                    for (auto& kv : fo->getProperties()) files[kv.name.toString()] = juce::File (kv.value.toString());
                done = true;
            }
            else if (type == "error") { failed = true; error = v["message"].toString(); }
        }
    }
    activeProcess->waitForProcessToFinish (2000);
    const bool cancelled = cancelRequested.exchange (-1) == id || threadShouldExit();
    activeProcess.reset();
    if (cancelled) { postUpdate (id, [] (Job& j) { j.state = Job::State::Cancelled; j.stage = "cancelled"; }); return; }
    if (failed || ! done)
    {
        if (error.isEmpty()) error = "worker exited without result" + (stderrTail.isNotEmpty() ? ":\n" + stderrTail.getLastCharacters (600) : juce::String());
        log ("Stem separation failed: " + error);
        postUpdate (id, [error] (Job& j) { j.state = Job::State::Failed; j.error = error; j.stage = "failed"; });
        return;
    }
    postUpdate (id, [files] (Job& j) { j.files = files; j.state = Job::State::Done; j.progress = 1.0f; j.stage = "done"; });
}
} // namespace mashup
