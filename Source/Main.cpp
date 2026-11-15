#include "Interface.h"
#include "Japanese.h"

namespace auralis {
class AuralisApplication : public JUCEApplication {
public:
    const String getApplicationName() override { return "Auralis"; }
    const String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const String& commandLine) override {
        initialiseJapanese();
        auto args = StringArray::fromTokens(commandLine, true); args.removeEmptyStrings();
        for (auto& arg : args) arg = arg.unquoted();
        if (args.contains("--scan-plugin")) {
            int p = args.indexOf("--scan-plugin"), o = args.indexOf("--scan-output");
            if (p + 1 < args.size() && o >= 0 && o + 1 < args.size()) {
                AudioPluginFormatManager formats; formats.addDefaultFormats(); KnownPluginList list;
                for (auto* format : formats.getFormats()) {
                    OwnedArray<PluginDescription> found; format->findAllTypesForFile(found, args[p + 1]);
                    for (auto* desc : found) list.addType(*desc);
                }
                if (auto xml = list.createXml()) xml->writeTo(File(args[o + 1]));
            }
            quit(); return;
        }
        if (args.contains("--self-test")) {
            int p = args.indexOf("--self-test");
            auto output = p + 1 < args.size() ? File(args[p + 1]) : File::getCurrentWorkingDirectory().getChildFile("test-output");
            setApplicationReturnValue(runSelfTests(output)); quit(); return;
        }
        window = std::make_unique<Window>(*this);
        if (!args.contains("--screenshot")) window->workstation->showProjectStart();
        if (args.contains("--screenshot")) {
            int p = args.indexOf("--screenshot");
            if (p + 1 < args.size()) {
                if (args.contains("--demo") && args.indexOf("--demo") + 1 < args.size()) {
                    try { auto& w = *window->workstation; File demo(args[args.indexOf("--demo") + 1]); if (demo.hasFileExtension("auralis")) w.engine.loadProject(demo); else w.engine.importAudio(demo, 0, 0); w.refresh(); } catch (...) {}
                }
                auto file = File(args[p + 1]);
                bool piano = args.contains("--piano");
                if (piano) { auto& w = *window->workstation; for (int i = 0; i < int(w.engine.tracks.size()); ++i) if (w.engine.tracks[size_t(i)]->midi && !w.engine.tracks[size_t(i)]->clips.empty()) { w.showPianoRoll(i, 0); break; } }
                Timer::callAfterDelay(1700, [this, file, piano] { auto picture = piano ? window->workstation->pianoRollPreview() : window->createComponentSnapshot(window->getLocalBounds()); file.getParentDirectory().createDirectory();
                    if (auto stream = file.createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(picture, *stream); } quit(); });
            }
        }
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override {
        if (window) window->workstation->requestClose([this] { quit(); }); else quit();
    }
    void anotherInstanceStarted(const String&) override {}
private:
    class Window : public DocumentWindow {
    public:
        Workstation* workstation = nullptr;
        Window(AuralisApplication& a) : DocumentWindow(TRANS("Auralis — Audio Workstation"), Colour(0xff11151b), allButtons), app(a) {
            setUsingNativeTitleBar(true); workstation = new Workstation(); setContentOwned(workstation, true);
            setResizable(true, false); setResizeLimits(1280, 820, 3840, 2160); centreWithSize(1400, 880); setVisible(true);
        }
        void closeButtonPressed() override { app.systemRequestedQuit(); }
    private:
        AuralisApplication& app;
    };
    std::unique_ptr<Window> window;
};
}
START_JUCE_APPLICATION(auralis::AuralisApplication)
