#pragma once
#include "Engine.h"
#include <thread>

namespace auralis {
class Workstation;
class PianoRollWindow;
class MixerPanel;
class ProjectStartPanel;
class PitchRollWindow;
enum class TrackControl { recordArm, mute, solo, monitor };
class TrackControlButton : public TextButton {
public:
    using TextButton::TextButton;
    std::function<void()> contextMenu;
    void paintButton(Graphics& g, bool hover, bool down) override {
        getLookAndFeel().drawButtonBackground(g, *this, findColour(getToggleState() ? buttonOnColourId : buttonColourId), hover, down);
        g.setColour(findColour(getToggleState() ? textColourOnId : textColourOffId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
        g.setFont(12.0f);
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(2, 0), Justification::centred, 1, 0.8f);
    }
    void mouseDown(const MouseEvent& e) override {
        popupGesture = e.mods.isPopupMenu();
        if (popupGesture) { if (contextMenu) contextMenu(); } else TextButton::mouseDown(e);
    }
    void mouseDrag(const MouseEvent& e) override { if (!popupGesture) TextButton::mouseDrag(e); }
    void mouseUp(const MouseEvent& e) override { if (popupGesture) { popupGesture = false; return; } TextButton::mouseUp(e); }
private:
    bool popupGesture = false;
};
class TrackControlRow : public Component, private ListBoxModel {
public:
    TrackControlRow(Workstation&, const String& trackId);
    ~TrackControlRow() override { name.onFocusLost = nullptr; name.onReturnKey = nullptr; inserts.setModel(nullptr); }
    void paint(Graphics&) override;
    void resized() override;
    void sync(const Track&, bool recording, bool busy);
    void mouseDown(const MouseEvent& e) override { if (auto* parent = getParentComponent()) parent->mouseDown(e.getEventRelativeTo(parent)); }
    void mouseDrag(const MouseEvent& e) override { if (auto* p = getParentComponent()) p->mouseDrag(e.getEventRelativeTo(p)); }
    void mouseUp(const MouseEvent& e) override { if (auto* p = getParentComponent()) p->mouseUp(e.getEventRelativeTo(p)); }
    void mouseMove(const MouseEvent& e) override { if (auto* p = getParentComponent()) { p->mouseMove(e.getEventRelativeTo(p)); setMouseCursor(p->getMouseCursor()); } }
private:
    Workstation& owner;
    String trackId;
    TextEditor name;
    ComboBox input;
    Slider gain, pan;
    ToggleButton midiOut {"MIDI out"};
    TextButton add {"+ Add"}, more {"..."};
    TrackControlButton fold {"-"};
    ListBox inserts {"Track inserts", this};
    int getNumRows() override;
    void paintListBoxItem(int, Graphics&, int, int, bool) override;
    void listBoxItemDoubleClicked(int, const MouseEvent&) override;
    void listBoxItemClicked(int, const MouseEvent&) override;
    String getTooltipForRow(int) override;
    TrackControlButton arm {"REC"}, mute {"MUTE"}, solo {"SOLO"}, monitor {"MON"};
};
class Timeline : public Component, public FileDragAndDropTarget, public SettableTooltipClient {
public:
    explicit Timeline(Workstation&);
    void paint(Graphics&) override;
    void paintRuler(Graphics& g) { const Graphics::ScopedSaveState state(g); g.reduceClipRegion(0, 0, getWidth(), ruler); paint(g); }
    void mouseDown(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;
    void mouseUp(const MouseEvent&) override;
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails&) override;
    void mouseMove(const MouseEvent&) override;
    void mouseExit(const MouseEvent&) override;
    void mouseDoubleClick(const MouseEvent&) override;
    bool isInterestedInFileDrag(const StringArray&) override { return true; }
    void filesDropped(const StringArray&, int, int) override;
    void syncTrackControls();
    int trackTop(int) const;
    int trackHeight(int) const;
    int trackAt(float) const;
    int totalHeight() const;
    void setCutMode(bool);
    bool isCutMode() const { return cutMode; }
    bool isInteracting() const { return draggingPlayhead || selectingTime || dragTrack >= 0 || headerDrag >= 0; }
    double scroll = 0, zoom = 70;
    static constexpr int header = 320, seekLaneHeight = 26, ruler = 52, row = 224;
    double timeAt(float x) const { return jmax(0.0, scroll + (x - header) / zoom); }
    float xAt(double time) const { return float(header + (time - scroll) * zoom); }
    int selectedClip = -1;
private:
    Workstation& owner;
    std::vector<std::unique_ptr<TrackControlRow>> trackControls;
    int dragTrack = -1, dragClip = -1, dragMode = 0;
    int headerDrag = -1, headerDrop = -1, resizeStartHeight = 224, dropTrack = -1;
    float headerStartY = 0;
    bool resizingTrack = false;
    Clip original;
    double mouseStart = 0;
    Point<float> clipDragOrigin;
    bool dragRemembered = false;
    struct EdgeHit { int track = -1, clip = -1, side = 0; };
    EdgeHit trimEdgeAt(Point<float>) const;
    void beginClipDrag(int track, int clip, int mode, const MouseEvent&);
    String hoverTrimClip;
    int hoverTrimTrack = -1, hoverTrimSide = 0;
    Rectangle<float> gainHandleBounds(const Clip&, int track) const;
    bool draggingPlayhead = false;
    double dragPosition = 0;
    bool selectingTime = false;
    double rangeAnchor = 0;
    bool cutMode = false, cutFree = false;
    int hoverRulerLane = -1;
    Point<float> cutHover {-1, -1};
};

class TimelineRuler : public Component, public SettableTooltipClient {
public:
    TimelineRuler(Timeline&, Viewport&);
    void resized() override;
    void syncTools(bool enabled);
    void paint(Graphics& g) override { timeline.paintRuler(g); }
    void mouseDown(const MouseEvent& e) override { timeline.mouseDown(forward(e)); repaint(); }
    void mouseDrag(const MouseEvent& e) override { timeline.mouseDrag(forward(e)); repaint(); }
    void mouseUp(const MouseEvent& e) override { timeline.mouseUp(forward(e)); repaint(); }
    void mouseMove(const MouseEvent& e) override { timeline.mouseMove(forward(e)); setMouseCursor(timeline.getMouseCursor()); setTooltip(timeline.getTooltip()); repaint(); }
    void mouseExit(const MouseEvent& e) override { timeline.mouseExit(forward(e)); repaint(); }
    void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override {
        if (e.mods.isCommandDown()) timeline.mouseWheelMove(forward(e), wheel); else viewport.mouseWheelMove(e.getEventRelativeTo(&viewport), wheel);
        repaint();
    }
private:
    MouseEvent forward(const MouseEvent& e) const { return e.getEventRelativeTo(&timeline).withNewPosition(e.position); }
    Timeline& timeline;
    Viewport& viewport;
    TrackControlButton selectTool, cutTool;
};

class PluginWindow : public DocumentWindow {
public:
    PluginWindow(AudioPluginInstance&);
    void closeButtonPressed() override { setVisible(false); }
};

class Workstation : public Component, public Timer {
public:
    explicit Workstation(bool connectDevices = true, const File& pluginRegistry = File());
    ~Workstation() override;
    Engine engine;
    Timeline timeline;
    Viewport viewport;
    TimelineRuler rulerBar {timeline, viewport};
    int selectedTrack = 0;
    bool dirty = false, snap = true, busy = false;
    bool hasTimeSelection = false;
    Range<double> timeSelection;
    void setTimeSelection(double start, double end);
    void togglePunchSelection();
    void toggleLoopSelection();
    String status = "Ready. Import audio or arm a track to record.";
    void paint(Graphics&) override;
    void resized() override;
    bool keyPressed(const KeyPress&) override;
    void timerCallback() override;
    void refresh();
    void setTimelineZoom(double pixelsPerSecond, float anchorX = Timeline::header);
    void fitProject();
    void selectTrack(int);
    void toggleTrackControl(const String& trackId, TrackControl);
    void trackMenu(const String& trackId);
    void confirmRemoveTrack(const String& trackId);
    void changed();
    void rememberEdit();
    void undoEdit();
    void clipMenu(int, int, double clickSeconds = -1);
    bool cutClipAt(int track, int clip, double seconds);
    bool selectClipById(const String& trackId, const String& clipId);
    bool setClipGain(const String& trackId, const String& clipId, double db);
    void showClipGainDialog(const String& trackId, const String& clipId);
    bool normalizeClip(const String& trackId, const String& clipId, double targetDb = -1);
    void showNormalizeDialog(const String& trackId, const String& clipId);
    void showPitchCorrectionDialog(const String& trackId, const String& clipId);
    bool startPitchCorrection(const String& trackId, const String& clipId, const PitchCorrectionSettings&, std::function<void()> after = {});
    void showPitchRoll(const String& trackId, const String& clipId, bool reanalyse = false);
    bool resetPitchCorrection(const String& trackId, const String& clipId);
    void importFiles(const StringArray&, int track = -1, double start = -1);
    void error(const String&);
    void requestClose(std::function<void()>);
    void setStatus(const String&);
    void showPianoRoll(int track, int clip);
    void exportMidiFile();
    Image pianoRollPreview();
    void toggleMixer();
    bool setMixerValue(const String& trackId, double value, bool isPan = false);
    void showProjectStart(bool visible = true);
    void followTransport();
    bool followPlayhead = true;
    void trackInsertMenu(const String&, const String& insertId = {}, bool add = false);
    bool trackInsertAction(const String&, const String&, int action);
    bool setTrackDetails(const String&, const String&, int input, bool midiOut);
    bool moveTrack(const String& id, int destination);
    bool setTrackHeight(const String& id, int height);
    bool toggleTrackExpanded(const String& id);
private:
    friend class TrackControlRow;
    friend void runTrackDetailsTests(const File&, StringArray&);
    friend void runFollowTests(const File&, StringArray&);
    friend void runTrackDragTests(const File&, StringArray&);
    friend void runFixedRulerTests(const File&, StringArray&);
    friend void runMasterInsertTests(const File&, StringArray&);
    friend void runEqualizerTests(const File&, StringArray&);
    friend void runNativeDynamicsTests(const File&, StringArray&);
    friend void runSpaceEffectsTests(const File&, StringArray&);
    friend void runProjectOpenTests(const File&, StringArray&);
    friend void runProjectSafetyTests(const File&, StringArray&);
    friend void runTrackDeleteTests(const File&, StringArray&);
    friend void runClipGainTests(const File&, StringArray&);
    friend void runNormalizeTests(const File&, StringArray&);
    friend void runPitchCorrectionTests(const File&, StringArray&);
    friend void runPitchRollTests(const File&, StringArray&);
    friend void runPitchPreviewTests(const File&, StringArray&);
    friend void runClipTrimTests(const File&, StringArray&);
    friend void runLoopTests(const File&, StringArray&);
    friend void runPluginRegistryTests(const File&, StringArray&);
    friend void runMixerTests(const File&, StringArray&);
    friend void runRecentProjectTests(const File&, StringArray&);
    friend class ProjectStartPanel;
    LookAndFeel_V4 theme;
    TooltipWindow tooltips {this, 650};
    Image brandLogo;
    TextButton fileButton {"Project"}, importButton {"Import audio"}, addButton {"+ Track"}, settingsButton {"Audio / MIDI"};
    TextButton playButton {"PLAY"}, stopButton {"STOP"}, recordButton {"RECORD"}, homeButton {"|<"};
    TextButton exportButton {"Export WAV"}, loopButton {"Loop"}, snapButton {"Snap"}, undoButton {"Undo edit"};
    TextButton newMidiButton {"+ MIDI clip"}, pianoButton {"Piano roll"};
    TextButton panicButton {"Panic"};
    TextButton metronomeButton {"Metronome"};
    TextButton recordRangeButton {"Record range"};
    TextButton transportOptions {"Options"}, pluginActions {"..."};
    TextButton mixerButton {"Mixer"};
    std::unique_ptr<MixerPanel> mixer;
    bool mixerVisible = false;
    std::unique_ptr<ProjectStartPanel> projectStart;
    std::vector<File> recentProjects;
    File recentProjectsFile;
    void loadRecentProjects();
    bool saveRecentProjects();
    bool rememberProject(const File&);
    void newProject();
    bool transportExpanded = false;
    ToggleButton monitor {"Input monitor"};
    ToggleButton hardwareMidi {"Send MIDI out"};
    TextEditor trackName, tempo, loopFrom, loopTo, recordFrom, recordTo;
    Slider volume, pan, masterVolume, zoom;
    ComboBox input, countIn;
    ListBox pluginList;
    TextButton scanButton {"Scan VST3"}, loadButton {"+ Insert"}, editorButton {"Open editor"}, bypassButton {"Bypass"}, removeButton {"Remove"};
    std::unique_ptr<FileChooser> chooser;
    std::vector<std::unique_ptr<PluginWindow>> pluginWindows;
    std::vector<std::unique_ptr<PianoRollWindow>> pianoWindows;
    std::vector<std::unique_ptr<PitchRollWindow>> pitchWindows;
    bool blockForPitchEdits();
    std::unique_ptr<MidiKeyboardComponent> keys;
    ScrollBar horizontal {false};
    double followResumeAt = 0;
    File projectFile;
    MemoryBlock savedPluginState;
    Component::SafePointer<AlertWindow> savePrompt;
    Component::SafePointer<AlertWindow> projectTrustPrompt;
    Component::SafePointer<AlertWindow> trackRemovalPrompt;
    Component::SafePointer<AlertWindow> clipGainPrompt;
    Component::SafePointer<AlertWindow> normalizePrompt;
    Component::SafePointer<AlertWindow> pitchPrompt;
    bool installPitchCorrection(const String&, const String&, const Clip&, const PitchCorrectionSettings&, const std::shared_ptr<SourceAudio>&);
    String trackMenuTarget;
    bool removeTrackById(String);
    KnownPluginList knownPlugins;
    File pluginRegistryFile;
    void loadPluginRegistry(const File& legacyReleaseDirectory = File());
    bool savePluginRegistry();
    std::thread worker;
    std::vector<std::vector<std::vector<Clip>>> undo;
    std::vector<TextButton*> buttons;
    void buildButton(TextButton&, std::function<void()>);
    void projectMenu();
    void pluginMenu();
    void save(bool saveAs, std::function<void()> after = {});
    void saveToFile(const File&, std::function<void()> after = {});
    void openSelectedProject(const File&);
    void loadProjectFile(const File&);
    void installProject(const ProjectDocument&, ProjectLoadApproval);
    MemoryBlock pluginStateSnapshot();
    void markProjectSaved();
    void open();
    void exportAudio();
    Range<double> defaultExportRange() const;
    void audioSettings();
    void audioDiagnostics();
    void scanPlugins();
    void insertPlugin();
    void showEditor();
    void runBackground(std::function<String()> work, std::function<void()> done = {});
    void updateTrack();
    void applyControls();
    File dataDirectory() const;
    struct PluginModel : public ListBoxModel {
        Workstation& owner;
        explicit PluginModel(Workstation& w) : owner(w) {}
        int getNumRows() override;
        void paintListBoxItem(int, Graphics&, int, int, bool) override;
        void listBoxItemDoubleClicked(int, const MouseEvent&) override;
    } pluginModel;
};
}
