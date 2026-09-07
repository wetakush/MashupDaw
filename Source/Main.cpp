#include <juce_gui_extra/juce_gui_extra.h>
#include "Project/Session.h"
#include "Project/ProjectController.h"
#include "Project/ProjectFile.h"
#include "UI/MainWindow.h"
#include "UI/Workspace.h"
#include "UI/Theme/Theme.h"
#include "Import/FFmpegDecoder.h"
#include "StemSeparation/StemSeparationService.h"
#include "Export/OfflineRenderer.h"
#include "UI/BottomPanel.h"
#include "Tracks/TrackModel.h"
#include <thread>

class MashupDawApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MashupDaw"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
       #if JUCE_LINUX
        // PipeWire's client-side module-rt asks rtkit for realtime scheduling and, on some setups (seen with
        // pipewire 1.6 + rtkit 0.14), ends up setting RLIMIT_RTTIME to 0 for this process. The kernel then
        // SIGKILLs us as soon as an RT thread runs. Realtime priority for the audio thread is handled by JUCE
        // itself, so tell libpipewire not to touch rtkit/rlimits.
        if (std::getenv ("DISABLE_RTKIT") == nullptr) setenv ("DISABLE_RTKIT", "1", 1);
       #endif
        fileLogger = juce::FileLogger::createDefaultAppLogger ("MashupDaw", "mashupdaw.log", "MashupDaw log");
        juce::Logger::setCurrentLogger (fileLogger);
        theme = std::make_unique<mashup::ui::Theme>();
        juce::LookAndFeel::setDefaultLookAndFeel (theme.get());
        session = std::make_unique<mashup::Session>();
        if (std::getenv ("MASHUP_SKIP") != nullptr && juce::String (std::getenv ("MASHUP_SKIP")).contains ("window")) { juce::Logger::writeToLog ("no window"); return; }
        mainWindow = std::make_unique<mashup::ui::MainWindow> (getApplicationName(), *session);
        tooltips = std::make_unique<juce::TooltipWindow> (nullptr, 600);

        // crash recovery: autosaves left by a previous session
        auto recoverable = mashup::ProjectController::findRecoverableAutosaves();
        if (! recoverable.isEmpty())
        {
            auto f = recoverable[0];
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Recover project?",
                "MashupDaw did not shut down cleanly last time. Restore the autosaved project from " + f.getLastModificationTime().toString (true, true) + "?",
                "Restore", "Discard", nullptr, juce::ModalCallbackFunction::create ([this, recoverable] (int r)
                {
                    if (r == 1) { auto res = session->getProjectController().recoverFrom (recoverable[0]); if (res.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Recovery failed", res.getErrorMessage()); }
                    for (auto& f2 : recoverable) f2.deleteFile();
                }));
        }
        handleCommandLine (commandLine);
    }

    void anotherInstanceStarted (const juce::String& commandLine) override { handleCommandLine (commandLine); }

    void handleCommandLine (const juce::String& commandLine)
    {
        juce::Logger::writeToLog ("command line: " + commandLine);
        juce::StringArray audio;
        for (auto& rawTok : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::String tok = rawTok.unquoted();
            // developer options: --screenshot=<png> [--delay=<ms>] renders the main window to a file and quits
            if (tok.startsWith ("--screenshot=")) { screenshotFile = juce::File::getCurrentWorkingDirectory().getChildFile (tok.fromFirstOccurrenceOf ("=", false, false).unquoted()); continue; }
            if (tok.startsWith ("--delay=")) { screenshotDelay = tok.fromFirstOccurrenceOf ("=", false, false).getIntValue(); continue; }
            if (tok == "--separate") { separateAfterImport = true; continue; }
            if (tok.startsWith ("--tab=")) { startTab = tok.fromFirstOccurrenceOf ("=", false, false); continue; }
            if (tok.startsWith ("--save=")) { saveFile = juce::File::getCurrentWorkingDirectory().getChildFile (tok.fromFirstOccurrenceOf ("=", false, false)); continue; }
            if (tok.startsWith ("--autolane")) { showAutoLanes = true; continue; }
            if (tok.startsWith ("--export=")) { exportFile = juce::File::getCurrentWorkingDirectory().getChildFile (tok.fromFirstOccurrenceOf ("=", false, false).unquoted()); continue; }
            if (tok.startsWith ("--")) continue;
            juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (tok.unquoted()));
            if (! f.existsAsFile()) continue;
            if (f.hasFileExtension (mashup::ProjectFile::extension)) { auto r = session->getProjectController().open (f); if (r.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed", r.getErrorMessage()); }
            else if (mashup::FFmpegDecoder::isSupportedExtension (f.getFileExtension())) audio.add (f.getFullPathName());
        }
        if (! audio.isEmpty()) mainWindow->getWorkspace().importFilesAtPlayhead (audio);
        if (startTab.isNotEmpty()) mainWindow->getWorkspace().getBottomPanel().showTab (startTab);
        if (showAutoLanes) juce::Timer::callAfterDelay (2500, [this] { for (auto t : session->getProject().tracks()) mashup::TrackModel (t).setAutomationShown (true, nullptr); });
        if (separateAfterImport)   // developer flag: separate the first imported source (end-to-end test of the stem pipeline)
            juce::Timer::callAfterDelay (1500, [this]
            {
                auto sources = session->getProject().sources();
                if (sources.getNumChildren() > 0) session->getStemSeparation().separate (sources.getChild (0)[mashup::ids::id], mashup::StemSeparationService::Mode::FourStems);
            });
        if (saveFile != juce::File())   // developer flag: save the project after import/analysis and quit
            juce::Timer::callAfterDelay (6000, [this] { auto r = session->getProjectController().saveAs (saveFile); juce::Logger::writeToLog ("save " + saveFile.getFullPathName() + (r.wasOk() ? " ok" : " failed: " + r.getErrorMessage())); if (screenshotFile == juce::File()) quit(); });
        if (exportFile != juce::File())   // developer flag: render the whole project to a file after import/analysis, then quit
            juce::Timer::callAfterDelay (6000, [this]
            {
                std::thread ([this]
                {
                    mashup::OfflineRenderer::Options o; o.sampleRate = 48000; o.normalizePeak = true;
                    mashup::FFmpegEncoder::Settings e; e.format = exportFile.hasFileExtension ("mp3") ? mashup::FFmpegEncoder::Format::MP3 : exportFile.hasFileExtension ("flac") ? mashup::FFmpegEncoder::Format::FLAC : exportFile.hasFileExtension ("m4a") ? mashup::FFmpegEncoder::Format::AAC : mashup::FFmpegEncoder::Format::WAV;
                    auto err = mashup::OfflineRenderer::renderToFile (*session, o, exportFile, e);
                    juce::MessageManager::callAsync ([this, err] { juce::Logger::writeToLog ("export " + exportFile.getFullPathName() + (err.isEmpty() ? " ok" : " failed: " + err)); if (screenshotFile == juce::File()) quit(); });
                }).detach();
            });
        if (screenshotFile != juce::File())
            juce::Timer::callAfterDelay (juce::jmax (500, screenshotDelay), [this]
            {
                auto* content = mainWindow->getContentComponent();
                auto img = content->createComponentSnapshot (content->getLocalBounds(), false, 1.0f);
                juce::PNGImageFormat png; juce::FileOutputStream os (screenshotFile);
                if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (img, os); }
                juce::Logger::writeToLog ("screenshot written: " + screenshotFile.getFullPathName());
                quit();
            });
    }

    void shutdown() override
    {
        tooltips.reset();
        mainWindow.reset();
        session.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        theme.reset();
        juce::Logger::setCurrentLogger (nullptr);
        delete fileLogger;
    }

    void systemRequestedQuit() override
    {
        if (session) session->getProjectController().confirmDiscardChanges ([this] { quit(); });
        else quit();
    }

private:
    std::unique_ptr<mashup::ui::Theme> theme;
    std::unique_ptr<mashup::Session> session;
    std::unique_ptr<mashup::ui::MainWindow> mainWindow;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::FileLogger* fileLogger = nullptr;
    juce::File screenshotFile; int screenshotDelay = 3000; bool separateAfterImport = false; juce::File exportFile; juce::String startTab; bool showAutoLanes = false; juce::File saveFile;
};

START_JUCE_APPLICATION (MashupDawApplication)
