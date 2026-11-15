#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include <memory>
#include <map>
#include <array>

namespace auralis {
using namespace juce;

struct SourceAudio {
    File file;
    AudioBuffer<float> samples;
    double rate = 48000.0;
    // Immutable after import; drawing never scans the audio buffer under the render lock.
    std::vector<std::vector<float>> waveform;
    void buildWaveform();
    float displayPeak(int start, int count) const;
};
struct MidiNote {
    double beat = 0, length = 1;
    int pitch = 60, velocity = 100, channel = 1;
};
struct MidiEvent {
    double beat = 0;
    MidiMessage message;
};
struct CapturedMidi {
    double seconds = 0;
    std::array<uint8, 3> data {};
    int size = 0;
};
struct PitchScoop {
    bool enabled = false;
    double start = 0, depthCents = 100, durationMs = 150, curve = 1;
    bool operator==(const PitchScoop&) const = default;
};
using PitchPreviewCurve = std::vector<std::array<double, 2>>;
struct PitchCurvePoint {
    double seconds = 0, note = 60;
    bool operator==(const PitchCurvePoint&) const = default;
};
struct PitchNoteEdit {
    double start = 0, end = 0, original = 60, target = 60;
    PitchScoop scoop;
    std::vector<PitchCurvePoint> curve;
    bool operator==(const PitchNoteEdit&) const = default;
};
struct PitchCorrectionSettings {
    int root = 0, scale = 0, targetNote = -1;
    double referenceHz = 440, amount = 0.75, speedMs = 80, maxCents = 200;
    bool preserveFormants = true;
    bool manual = false;
    std::vector<PitchNoteEdit> notes;
    bool valid() const;
    bool operator==(const PitchCorrectionSettings&) const = default;
};
struct Clip {
    String id = Uuid().toString();
    int64 order = 0;
    std::shared_ptr<SourceAudio> source;
    std::shared_ptr<SourceAudio> pitchOriginal;
    PitchCorrectionSettings pitchSettings;
    double start = 0, offset = 0, length = 0;
    double fadeIn = 0.005, fadeOut = 0.005;
    float gainDb = 0;
    String name;
    bool midi = false;
    std::vector<MidiNote> notes;
    std::vector<MidiEvent> events;
    double scale(double bpm) const { return midi ? 60.0 / bpm : 1.0; }
    double startTime(double bpm) const { return start * scale(bpm); }
    double lengthTime(double bpm) const { return length * scale(bpm); }
    double endTime(double bpm) const { return (start + length) * scale(bpm); }
};
struct Insert {
    String id = Uuid().toString();
    PluginDescription description;
    std::unique_ptr<AudioPluginInstance> instance;
    bool bypass = false;
};
struct RecordingWaveform {
    static constexpr int stride = 256, capacity = 262144;
    std::unique_ptr<std::atomic<float>[]> peaks;
    std::atomic<int64> samples {0};
    float partialPeak = 0;
    void reset();
    void append(const float* const* channels, int count);
    float peak(int64 first, int64 last) const;
};
struct Track {
    int height = 224;
    int expandedHeight = 224;
    String id = Uuid().toString(), name;
    Colour colour;
    float gain = 0.8f, pan = 0.0f;
    bool mute = false, solo = false, armed = false, monitor = false;
    int input = 0; // 0: first stereo pair, 1..N: mono input
    std::vector<Clip> clips;
    std::vector<std::unique_ptr<Insert>> inserts;
    AudioBuffer<float> work {2, 8192};
    std::vector<int> clipOwners = std::vector<int>(8192, -1);
    String playingClipId;
    uint16 playingClipChannels = 0;
    std::unique_ptr<AudioFormatWriter::ThreadedWriter> recorder;
    File recordedFile;
    double recordStart = 0;
    int64 recordedSamples = 0;
    RecordingWaveform recordingWaveform;
    std::atomic<float> peak {0};
    bool midi = false, recordingMidi = false, sendMidi = false;
    int midiChannel = 0;
    double recordTempo = 120;
    std::vector<CapturedMidi> recordedMidi;
    Synthesiser synth;
    MidiBuffer midiBuffer;
};

class Engine : public AudioIODeviceCallback, public AudioPlayHead {
public:
    Engine();
    ~Engine() override;
    AudioDeviceManager devices;
    AudioFormatManager formats;
    AudioPluginFormatManager pluginFormats;
    MidiKeyboardState keyboard;
    CriticalSection lock;
    std::vector<std::unique_ptr<Track>> tracks;
    std::vector<std::unique_ptr<Insert>> masterInserts;
    std::atomic<bool> playing {false}, recording {false}, exporting {false};
    std::atomic<double> cursor {0}, rate {48000}, bpm {120};
    std::atomic<float> master {0.8f}, outputPeak {0};
    std::atomic<bool> pitchPreviewActive {false};
    std::atomic<int> dropped {0}, clipped {0};
    std::atomic<int> lockMisses {0}, oversizedBlocks {0}, xrunBaseline {0};
    std::atomic<int> inputLatencySamples {0}, outputLatencySamples {0};
    std::atomic<int> midiTrack {0};
    std::atomic<bool> midiDiscontinuity {true}, deviceInterrupted {false};
    std::atomic<bool> loop {false};
    std::atomic<bool> metronome {false};
    std::atomic<int> countInBars {0};
    std::atomic<int64> countInRemaining {0};
    std::atomic<bool> recordRange {false}, recordingComplete {false};
    std::atomic<double> recordFrom {0}, recordTo {8};
    std::atomic<double> loopStart {0}, loopEnd {8};
    int block = 512;
    File recordingDirectory;
    File deviceStateFile;
    String initialisationError;

    void initialise();
    void resetAudioDiagnostics();
    int audioXRuns() const;
    String audioDiagnostics() const;
    Track& addTrack(String name = {}, bool midi = false);
    Clip& addMidiClip(int track, double startSeconds, double lengthBeats = 8);
    void stampNewClip(Clip&);
    static bool newerClip(const Clip& a, int aIndex, const Clip& b, int bIndex) { return a.order > b.order || (a.order == b.order && aIndex > bIndex); }
    void importMidi(const File&, double startSeconds);
    void exportMidi(const File&);
    void selectMidiTrack(int);
    void setTempo(double);
    void splitClip(int track, int clip, double atSeconds);
    bool canSplitClip(int track, int clip, double atSeconds) const;
    void quantizeClip(Clip&, double gridBeats);
    std::shared_ptr<SourceAudio> readAudio(const File&);
    void importAudio(const File&, int track, double start);
    double duration() const;
    double effectTailSeconds() const;
    void play();
    void stop();
    void seek(double);
    bool startPitchPreview(const String& ownerId, const String& trackId, std::shared_ptr<SourceAudio>, double start, double end, double semitones, float gainDb = 0, double position = -1, std::shared_ptr<const PitchPreviewCurve> = {});
    void updatePitchPreview(const String& ownerId, double semitones);
    void updatePitchPreviewCurve(const String& ownerId, std::shared_ptr<const PitchPreviewCurve>);
    void stopPitchPreview(const String& ownerId = {}, bool immediate = false);
    bool setPitchLoop(const String& ownerId, double start, double end);
    void clearPitchLoop(const String& ownerId = {});
    bool ownsPitchLoop(const String& ownerId);
    void beginRecording();
    void finishRecording();
    void addPlugin(int track, const PluginDescription&, const MemoryBlock* state = nullptr);
    void removePlugin(int track, int insert);
    void exportWav(const File&, double start, double end, int bits = 24);
    void exportMp3(const File&, double start, double end, int kbps = 320);
    void saveProject(const File&);
    void loadProject(const File&);
    void reset();
    void render(float* const*, int numOutputs, int n, const float* const* input = nullptr, int numInputs = 0, bool collectLiveMidi = false);
    Optional<PositionInfo> getPosition() const override;
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int, const AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(AudioIODevice*) override;
    void audioDeviceStopped() override;
private:
    friend int runSelfTests(const File&);
    friend void runMidiTests(const File&, StringArray&);
    friend void runRealtimeTests(const File&, StringArray&);
    friend void runTrackControlTests(const File&, StringArray&);
    friend void runRecordingRangeTests(const File&, StringArray&);
    friend void runRangeExportTests(const File&, StringArray&);
    friend void runOverlapTests(const File&, StringArray&);
    friend void runLoopTests(const File&, StringArray&);
    friend void runTrackDragTests(const File&, StringArray&);
    friend void runMasterInsertTests(const File&, StringArray&);
    friend void runEqualizerTests(const File&, StringArray&);
    friend void runNativeDynamicsTests(const File&, StringArray&);
    friend void runSpaceEffectsTests(const File&, StringArray&);
    friend void runRecordingAlignmentTests(const File&, StringArray&);
    friend void runRecordingWaveformTests(const File&, StringArray&);
    void exportMix(const File&, double start, double end, int bits, int kbps);
    void startRecordingWriters();
    TimeSliceThread diskThread {"Recording disk writer"};
    MidiMessageCollector midiCollector;
    MidiBuffer incomingMidi;
    MidiBuffer loopMidiSource, loopExternalMidi;
    void renderChunk(float* const*, int outputs, int n, const float* const*, int inputs);
    AudioBuffer<float> capture {2, 8192};
    AudioBuffer<float> masterWork {2, 8192};
    struct PitchPreviewState {
        String ownerId, trackId;
        std::shared_ptr<SourceAudio> source;
        std::shared_ptr<const PitchPreviewCurve> curve;
        double first = 0, last = 0, position = 0, ratio = 1, targetRatio = 1;
        float gain = 1, envelope = 0;
        bool releasing = false;
    } pitchPreview;
    int pitchPreviewResumeFade = 0;
    String pitchLoopOwner;
    std::atomic<bool> pitchLoopActive {false};
    std::atomic<double> pitchLoopStart {0}, pitchLoopEnd {0};
    bool renderPitchPreview(float* const*, int outputs, int count);
    MidiBuffer masterMidi;
    MidiBuffer midiScratch;
    MidiBuffer externalMidi;
    void prepareTrack(Track&);
    int64 countInTotal = 0;
    double recordingEnd = std::numeric_limits<double>::infinity();
    double recordingPosition = 0;
    int64 recordingLatencySamples = 0;
    int64 nextClipOrder = 1;
    void buildClipOwners(Track&, double position, int n, int first, int last);
    void buildTrackMidi(Track&, double position, int n, bool discontinuity, int first = 0, int last = -1);
    void captureMidi(Track&, double position, int first, int last);
    void finishMidiCapture(Track&);
    void initialiseSynth(Track&);
};

int runSelfTests(const File& outputDirectory);
void runMidiTests(const File&, StringArray& passed);
void runRealtimeTests(const File&, StringArray& passed);
void runPianoEditTests(const File&, StringArray& passed);
void runTrackControlTests(const File&, StringArray& passed);
void runRecordingRangeTests(const File&, StringArray& passed);
void runCutTests(const File&, StringArray& passed);
void runCutFocusTests(const File&, StringArray& passed);
void runRangeExportTests(const File&, StringArray& passed);
void runProjectOpenTests(const File&, StringArray& passed);
void runTrackDeleteTests(const File&, StringArray& passed);
void runClipGainTests(const File&, StringArray& passed);
void runNormalizeTests(const File&, StringArray& passed);
void runPitchCorrectionTests(const File&, StringArray& passed);
void runPitchRollTests(const File&, StringArray& passed);
void runPitchPreviewTests(const File&, StringArray& passed);
void runPitchFollowTests(const File&, StringArray& passed);
void runPitchScoopTests(const File&, StringArray& passed);
void runPitchCurveTests(const File&, StringArray& passed);
void runPitchZoomTests(const File&, StringArray& passed);
void runPitchLoopTests(const File&, StringArray& passed);
void runEqualizerTests(const File&, StringArray& passed);
void runNativeDynamicsTests(const File&, StringArray& passed);
void runSpaceEffectsTests(const File&, StringArray& passed);
void runClipTrimTests(const File&, StringArray& passed);
void runOverlapTests(const File&, StringArray& passed);
void runLoopTests(const File&, StringArray& passed);
void runOverlapTrimTests(const File&, StringArray& passed);
void runRecordingAlignmentTests(const File&, StringArray& passed);
void runRecordingWaveformTests(const File&, StringArray& passed);
void runPluginRegistryTests(const File&, StringArray& passed);
void runTimelineLaneTests(const File&, StringArray& passed);
void runMixerTests(const File&, StringArray& passed);
void runRecentProjectTests(const File&, StringArray& passed);
void runTrackDetailsTests(const File&, StringArray& passed);
void runFollowTests(const File&, StringArray& passed);
void runTrackDragTests(const File&, StringArray& passed);
void runFixedRulerTests(const File&, StringArray& passed);
void runPluginInfoTests(const File&, StringArray& passed);
void runMasterInsertTests(const File&, StringArray& passed);
}
