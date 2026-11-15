#include "Engine.h"
#include "NativeEffects.h"
#include "SpaceEffects.h"
#include <cmath>
#include <stdexcept>
#include <lame.h>

namespace auralis {
namespace {
void require(bool ok, const String& why) { if (!ok) throw std::runtime_error(why.toStdString()); }
var object() { return var(new DynamicObject()); }
void set(var& v, const Identifier& key, const var& value) { v.getDynamicObject()->setProperty(key, value); }
double number(const var& v, const char* key, double fallback) { return v.hasProperty(key) ? double(v[key]) : fallback; }
}

void SourceAudio::buildWaveform() {
    waveform.clear();
    if (samples.getNumChannels() == 0 || samples.getNumSamples() == 0) return;
    std::vector<float> peaks; peaks.reserve(size_t((samples.getNumSamples() + 63) / 64));
    for (int at = 0; at < samples.getNumSamples(); at += 64) peaks.push_back(samples.getMagnitude(0, at, jmin(64, samples.getNumSamples() - at)));
    waveform.push_back(std::move(peaks));
    while (waveform.back().size() > 1) {
        const auto& previous = waveform.back(); std::vector<float> next; next.reserve((previous.size() + 1) / 2);
        for (size_t i = 0; i < previous.size(); i += 2) next.push_back(jmax(previous[i], previous[jmin(i + 1, previous.size() - 1)]));
        waveform.push_back(std::move(next));
    }
}
float SourceAudio::displayPeak(int start, int count) const {
    if (samples.getNumSamples() == 0 || count <= 0) return 0;
    start = jlimit(0, samples.getNumSamples() - 1, start); count = jmin(count, samples.getNumSamples() - start);
    if (waveform.empty() || count < 64) return samples.getMagnitude(0, start, count);
    size_t level = 0; int width = 64;
    while (level + 1 < waveform.size() && width <= count / 2) { ++level; width *= 2; }
    float peak = 0; const auto& data = waveform[level];
    for (int bucket = start / width; bucket <= (start + count - 1) / width; ++bucket) peak = jmax(peak, data[size_t(bucket)]);
    return peak;
}

Engine::Engine() {
    formats.registerBasicFormats();
    pluginFormats.addDefaultFormats();
    diskThread.startThread();
    recordingDirectory = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Auralis/Recordings");
    incomingMidi.ensureSize(16384); masterMidi.ensureSize(65536);
    loopMidiSource.ensureSize(16384); loopExternalMidi.ensureSize(65536);
    midiScratch.ensureSize(16384);
    externalMidi.ensureSize(65536);
}
void RecordingWaveform::reset() {
    // Allocate only before recording. The callback publishes peaks without GUI locks.
    if (!peaks) peaks = std::make_unique<std::atomic<float>[]>(capacity);
    for (int i = 0; i < capacity; ++i) peaks[i].store(0, std::memory_order_relaxed);
    partialPeak = 0; samples.store(0, std::memory_order_release);
}
void RecordingWaveform::append(const float* const* channels, int count) {
    if (!peaks || count <= 0) return;
    int64 total = samples.load(std::memory_order_relaxed); int at = 0;
    while (at < count) {
        const int part = jmin(count - at, stride - int(total % stride));
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < part; ++i) {
            const float value = channels[ch][at + i];
            if (std::isfinite(value)) partialPeak = jmax(partialPeak, std::abs(value));
        }
        if (total / stride < capacity) peaks[size_t(total / stride)].store(partialPeak, std::memory_order_relaxed);
        total += part; at += part; if (total % stride == 0) partialPeak = 0;
    }
    samples.store(total, std::memory_order_release);
}
float RecordingWaveform::peak(int64 first, int64 last) const {
    const int64 end = jmin(samples.load(std::memory_order_acquire), int64(capacity) * stride);
    first = jmax<int64>(0, first); last = jmin(last, end);
    if (!peaks || last <= first) return 0;
    float result = 0;
    for (int64 i = first / stride; i <= (last - 1) / stride; ++i) result = jmax(result, peaks[size_t(i)].load(std::memory_order_relaxed));
    return result;
}
Engine::~Engine() {
    if (deviceStateFile != File()) if (auto state = devices.createStateXml()) state->writeTo(deviceStateFile);
    devices.removeAudioCallback(this);
    devices.removeMidiInputDeviceCallback({}, &midiCollector);
    for (auto& track : tracks) track->recorder.reset();
    diskThread.stopThread(5000);
}
void Engine::initialise() {
    auto settings = deviceStateFile == File() ? nullptr : parseXML(deviceStateFile);
    initialisationError = devices.initialise(2, 2, settings.get(), true);
    if (initialisationError.isNotEmpty()) initialisationError = devices.initialiseWithDefaultDevices(0, 2);
    devices.addMidiInputDeviceCallback({}, &midiCollector);
    devices.addAudioCallback(this);
}
void Engine::resetAudioDiagnostics() { lockMisses = 0; oversizedBlocks = 0; dropped = 0; clipped = 0; xrunBaseline = devices.getXRunCount(); }
int Engine::audioXRuns() const { return jmax(0, devices.getXRunCount() - xrunBaseline.load()); }
String Engine::audioDiagnostics() const {
    auto* device = devices.getCurrentAudioDevice();
    String info = "Auralis " + String(ProjectInfo::versionString) + " audio diagnostics\n";
    info += "Backend: " + (device ? device->getTypeName() : "No device") + "\nDevice: " + (device ? device->getName() : "None") + "\n";
    info += "Sample rate: " + String(rate.load(), 0) + " Hz\nEffective buffer: " + String(block) + " samples (" + String(block * 1000.0 / rate, 2) + " ms)\n";
    if (device) info += "Driver input/output latency: " + String(device->getInputLatencyInSamples()) + " / " + String(device->getOutputLatencyInSamples()) + " samples\n";
    info += "Audio recording alignment: automatic, " + String((int64) jmax(0, inputLatencySamples.load()) + jmax(0, outputLatencySamples.load())) + " samples (driver input + output)\n";
    info += "DSP load: " + String(devices.getCpuUsage() * 100, 2) + "%\nDriver/callback XRuns: " + String(audioXRuns()) + "\n";
    info += "Blocks skipped due to editing lock: " + String(lockMisses.load()) + "\nOversized callback blocks: " + String(oversizedBlocks.load()) + "\n";
    info += "Recording samples/events dropped: " + String(dropped.load()) + "\nClipping blocks: " + String(clipped.load()) + "\n";
    return info;
}
Track& Engine::addTrack(String name, bool midi) {
    const ScopedLock guard(lock);
    auto t = std::make_unique<Track>();
    t->midi = midi;
    t->name = name.isEmpty() ? String(midi ? "MIDI " : "Audio ") + String(tracks.size() + 1) : name;
    t->midiBuffer.ensureSize(65536);
    initialiseSynth(*t);
    static const uint32 colours[] = {0xff80e3c0, 0xff9ba7ff, 0xffedb07d, 0xffe58dc6, 0xff7fc8eb, 0xffd5db80};
    t->colour = Colour(colours[tracks.size() % 6]);
    t->work.setSize(2, jmax(8192, block));
    t->clipOwners.resize(size_t(jmax(8192, block)), -1);
    tracks.push_back(std::move(t));
    midiDiscontinuity = true;
    return *tracks.back();
}
std::shared_ptr<SourceAudio> Engine::readAudio(const File& file) {
    std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(file));
    require(reader != nullptr, "Cannot read audio: " + file.getFullPathName());
    require(reader->lengthInSamples > 0 && reader->lengthInSamples <= 64000000,
            "Audio is empty or exceeds the 64 million sample limit per file. Split long recordings first.");
    auto audio = std::make_shared<SourceAudio>();
    audio->file = file;
    audio->rate = reader->sampleRate;
    audio->samples.setSize(jmin(2, int(reader->numChannels)), int(reader->lengthInSamples));
    require(reader->read(&audio->samples, 0, audio->samples.getNumSamples(), 0, true, true), "Audio read failed.");
    audio->buildWaveform();
    return audio;
}
void Engine::importAudio(const File& file, int index, double start) {
    require(!recording, "Stop recording before importing audio.");
    auto source = readAudio(file);
    const ScopedLock guard(lock);
    if (isPositiveAndBelow(index, int(tracks.size())) && tracks[size_t(index)]->midi) index = -1;
    if (index < 0 || index >= int(tracks.size())) { addTrack(file.getFileNameWithoutExtension()); index = int(tracks.size()) - 1; }
    Clip clip;
    clip.source = source; clip.start = jmax(0.0, start);
    clip.length = source->samples.getNumSamples() / source->rate;
    clip.name = file.getFileNameWithoutExtension();
    stampNewClip(clip); tracks[size_t(index)]->clips.push_back(clip);
}
double Engine::duration() const {
    double end = 0;
    for (auto& t : tracks) for (auto& c : t->clips) end = jmax(end, c.endTime(bpm));
    return end;
}
double Engine::effectTailSeconds() const {
    const ScopedLock guard(lock);
    auto tail = [this](const auto& chain) { double length = 0; for (const auto& p : chain) if (!p->bypass) { const auto* delay = dynamic_cast<const DelayEffect*>(p->instance.get()); const double seconds = delay ? delay->tailSeconds(bpm.load()) : p->instance->getTailLengthSeconds(); if (seconds > 0) length += jmin(120.0, seconds); } return jmin(120.0, length); };
    bool anySolo = false; for (const auto& t : tracks) anySolo |= t->solo;
    double tracksTail = 0; for (const auto& t : tracks) if (!t->mute && (!anySolo || t->solo) && !t->clips.empty()) tracksTail = jmax(tracksTail, tail(t->inserts));
    return jmin(120.0, tracksTail + tail(masterInserts));
}
void Engine::stampNewClip(Clip& clip) { const ScopedLock guard(lock); clip.order = nextClipOrder++; }
void Engine::buildClipOwners(Track& track, double position, int n, int first, int last) {
    std::fill_n(track.clipOwners.begin(), n, -1);
    if (!playing && !exporting) return;
    for (int index = 0; index < int(track.clips.size()); ++index) {
        const auto& clip = track.clips[size_t(index)];
        double start = clip.startTime(bpm), end = clip.endTime(bpm);
        if (start >= position + last / rate || end <= position + first / rate) continue;
        int begin = int(jlimit(double(first), double(last), std::ceil((start - position) * rate - 1e-7)));
        int finish = int(jlimit(double(first), double(last), std::ceil((end - position) * rate - 1e-7)));
        for (int sample = begin; sample < finish; ++sample) {
            int& winner = track.clipOwners[size_t(sample)];
            if (winner < 0 || newerClip(clip, index, track.clips[size_t(winner)], winner)) winner = index;
        }
    }
}
void Engine::prepareTrack(Track& track) {
    track.work.setSize(2, jmax(8192, block));
    track.clipOwners.resize(size_t(jmax(8192, block)), -1);
    track.synth.setCurrentPlaybackSampleRate(rate);
    for (auto& p : track.inserts) {
        p->instance->releaseResources();
        p->instance->setRateAndBufferSizeDetails(rate, block);
        p->instance->prepareToPlay(rate, block);
    }
}
void Engine::play() { stopPitchPreview({}, true); midiDiscontinuity = true; playing = true; }
void Engine::stop() {
    stopPitchPreview({}, true);
    if (recording) finishRecording();
    playing = false;
    midiDiscontinuity = true;
    const ScopedLock guard(lock);
    for (auto& t : tracks) for (auto& p : t->inserts) p->instance->reset();
    for (auto& p : masterInserts) p->instance->reset();
    // A stopped transport must not replay queued keyboard presses on the next callback.
    keyboard.reset(); midiCollector.reset(rate); incomingMidi.clear();
    for (auto& t : tracks) t->synth.allNotesOff(0, false);
    if (auto* port = devices.getDefaultMidiOutput()) {
        port->clearAllPendingMessages(); MidiBuffer panic;
        for (int ch = 1; ch <= 16; ++ch) { panic.addEvent(MidiMessage::controllerEvent(ch, 64, 0), 0); panic.addEvent(MidiMessage::allSoundOff(ch), 0); }
        port->sendBlockOfMessagesNow(panic);
    }
}
void Engine::seek(double seconds) {
    const ScopedLock guard(lock);
    require(!recording, "Stop recording before moving the playhead.");
    stopPitchPreview({}, true);
    cursor = jmax(0.0, seconds);
    midiDiscontinuity = true;
    for (auto& t : tracks) for (auto& p : t->inserts) p->instance->reset();
    for (auto& p : masterInserts) p->instance->reset();
}
void Engine::beginRecording() {
    clearPitchLoop();
    stopPitchPreview({}, true);
    auto* device = devices.getCurrentAudioDevice();
    require(device != nullptr, "Enable an audio output device before recording.");
    const int available = device->getActiveInputChannels().countNumberOfSetBits();
    for (auto& t : tracks) if (t->armed && !t->midi) require(available > 0 && t->input <= available,
        "Enable the selected audio input for track: " + t->name + ". MIDI tracks do not need an audio input.");
    startRecordingWriters();
}
void Engine::startRecordingWriters() {
    const ScopedLock guard(lock);
    require(!recording && !exporting, "A recording or export is already running.");
    bool armed = false;
    for (auto& t : tracks) armed |= t->armed;
    require(armed, "Arm at least one track with ARM before recording.");
    const double target = std::round((recordRange ? recordFrom.load() : cursor.load()) * rate) / rate;
    const double end = recordRange ? std::round(recordTo * rate) / rate : std::numeric_limits<double>::infinity();
    require(std::isfinite(target) && target >= 0 && (!recordRange || (std::isfinite(end) && end > target)), "Choose a recording range with its end after its start.");
    require(recordingDirectory.createDirectory().wasOk(), "Cannot create recording directory.");
    // Input delivered now belongs to the backing track heard one round trip ago.
    // Snapshot once per take; no device queries or allocations in the callback.
    bool audioArmed = false;
    for (const auto& t : tracks) audioArmed |= t->armed && !t->midi;
    recordingLatencySamples = audioArmed ? (int64) jmax(0, inputLatencySamples.load()) + jmax(0, outputLatencySamples.load()) : 0;
    try {
        for (auto& t : tracks) if (t->armed) {
            t->recordStart = target; t->recordTempo = bpm;
            if (t->midi) {
                t->recordedMidi.clear(); t->recordedMidi.reserve(262144); t->recordingMidi = true; continue;
            }
            t->recordedFile = recordingDirectory.getNonexistentChildFile("Take-" + Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + "-" + File::createLegalFileName(t->name), ".wav");
            auto stream = t->recordedFile.createOutputStream();
            require(stream != nullptr, "Cannot create recording file.");
            WavAudioFormat wav;
            auto writer = std::unique_ptr<AudioFormatWriter>(wav.createWriterFor(stream.get(), rate, 2, 24, {}, 0));
            require(writer != nullptr, "Cannot create WAV writer.");
            stream.release();
            t->recorder = std::make_unique<AudioFormatWriter::ThreadedWriter>(writer.release(), diskThread, 262144);
            t->recordStart = target;
            t->recordedSamples = 0;
            t->recordingWaveform.reset();
        }
    } catch (...) { for (auto& t : tracks) { t->recorder.reset(); t->recordingMidi = false; } throw; }
    cursor = recordRange ? jmin(target, std::round(cursor * rate) / rate) : target;
    recordingPosition = cursor;
    recordingEnd = end; recordingComplete = false;
    countInTotal = std::llround(jlimit(0, 2, countInBars.load()) * 4 * 60.0 / bpm * rate);
    countInRemaining = countInTotal;
    dropped = 0; deviceInterrupted = false; midiDiscontinuity = true; recording = true; playing = true;
}
void Engine::finishRecording() {
    const ScopedLock guard(lock);
    recording = false; playing = false;
    countInRemaining = 0; recordingComplete = false;
    midiDiscontinuity = true;
    for (auto& t : tracks) if (t->recordingMidi) finishMidiCapture(*t);
    for (auto& t : tracks) if (t->recorder) {
        t->recorder.reset(); // flush and close before reader opens it
        if (t->recordedSamples > 0) {
            auto source = readAudio(t->recordedFile);
            Clip c; c.source = source; c.start = t->recordStart;
            c.length = source->samples.getNumSamples() / source->rate;
            c.name = t->recordedFile.getFileNameWithoutExtension(); stampNewClip(c); t->clips.push_back(c);
        } else t->recordedFile.deleteFile();
    }
}
void Engine::addPlugin(int index, const PluginDescription& description, const MemoryBlock* state) {
    require(!recording, "Stop recording before changing plugins.");
    require(index == -1 || isPositiveAndBelow(index, int(tracks.size())), "Select a track first.");
    require(index != -1 || !description.isInstrument, "Master inserts accept audio effects only.");
    String error;
    std::unique_ptr<AudioPluginInstance> instance;
    if (isNativeEffect(description)) instance = createNativeEffect(description);
    else instance = pluginFormats.createPluginInstance(description, rate, block, error);
    require(instance != nullptr, "Plugin could not be opened: " + error);
    auto layout = instance->getBusesLayout();
    if (layout.outputBuses.size() > 0) layout.outputBuses.getReference(0) = AudioChannelSet::stereo();
    if (layout.inputBuses.size() > 0) layout.inputBuses.getReference(0) = AudioChannelSet::stereo();
    for (int i = 1; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference(i) = AudioChannelSet::disabled();
    for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference(i) = AudioChannelSet::disabled();
    require(instance->setBusesLayout(layout) && instance->getTotalNumOutputChannels() <= 2 && instance->getTotalNumInputChannels() <= 2,
            "This host currently requires a stereo-compatible plugin without sidechain buses.");
    require(index != -1 || (instance->getTotalNumInputChannels() == 2 && instance->getTotalNumOutputChannels() == 2), "Master inserts require a stereo audio effect.");
    instance->setPlayHead(this);
    if (state && state->getSize() > 0) instance->setStateInformation(state->getData(), int(state->getSize()));
    instance->setRateAndBufferSizeDetails(rate, block);
    instance->prepareToPlay(rate, block);
    auto insert = std::make_unique<Insert>(); insert->description = description; insert->instance = std::move(instance);
    const ScopedLock guard(lock);
    (index == -1 ? masterInserts : tracks[size_t(index)]->inserts).push_back(std::move(insert));
    midiDiscontinuity = true;
}
void Engine::removePlugin(int track, int insert) {
    const ScopedLock guard(lock);
    require(!recording, "Stop recording before changing plugins.");
    auto& chain = track == -1 ? masterInserts : tracks.at(size_t(track))->inserts;
    chain.erase(chain.begin() + insert);
    midiDiscontinuity = true;
}
Optional<AudioPlayHead::PositionInfo> Engine::getPosition() const {
    PositionInfo info; info.setTimeInSamples(int64(cursor * rate)); info.setTimeInSeconds(cursor.load());
    info.setBpm(bpm.load()); info.setPpqPosition(cursor * bpm / 60.0); info.setTimeSignature(TimeSignature {4, 4});
    const bool editorLoop = pitchLoopActive && !recording && !exporting;
    info.setIsPlaying(playing || exporting); info.setIsRecording(recording && countInRemaining == 0 && !recordingComplete && (!recordRange || cursor >= recordFrom)); info.setIsLooping((loop || editorLoop) && !exporting);
    info.setLoopPoints(LoopPoints {(editorLoop ? pitchLoopStart.load() : loopStart.load()) * bpm / 60.0, (editorLoop ? pitchLoopEnd.load() : loopEnd.load()) * bpm / 60.0});
    return info;
}
void Engine::render(float* const* output, int outputs, int n, const float* const* input, int inputs, bool collectLiveMidi) {
    if (n <= 0) return;
    for (int ch = 0; ch < outputs; ++ch) if (output[ch]) FloatVectorOperations::clear(output[ch], n);
    const ScopedTryLock guard(lock);
    if (!guard.isLocked()) { ++lockMisses; if (recording) dropped += n; return; }
    if (n > capture.getNumSamples()) { ++oversizedBlocks; dropped += n; return; }
    if (pitchPreviewActive && renderPitchPreview(output, outputs, n)) { if (outputPeak > 1) ++clipped; return; }
    if (collectLiveMidi) {
        // The collector appends to its destination. Never replay a previous block.
        // Drain only after acquiring the edit lock, so a skipped block cannot lose a note-off.
        incomingMidi.clear();
        midiCollector.removeNextBlockOfMessages(incomingMidi, n);
        keyboard.processNextMidiBuffer(incomingMidi, 0, n, true);
    }
    const double timestamp = Time::getMillisecondCounterHiRes();
    auto sendMidi = [&] { if (!exporting && !externalMidi.isEmpty()) if (auto* port = devices.getDefaultMidiOutput()) port->sendBlockOfMessages(externalMidi, timestamp, rate); };
    const bool editorLoop = pitchLoopActive && !recording && !exporting;
    const double sampleRate = rate.load(), from = editorLoop ? pitchLoopStart.load() : loopStart.load(), to = editorLoop ? pitchLoopEnd.load() : loopEnd.load();
    const bool validLoop = (editorLoop || loop) && playing && !recording && !exporting && std::isfinite(from) && std::isfinite(to) && from >= 0 && to > from;
    const int64 startFrame = validLoop ? std::llround(from * sampleRate) : 0, endFrame = validLoop ? std::llround(to * sampleRate) : 0;
    if (!validLoop || endFrame <= startFrame) {
        renderChunk(output, outputs, n, input, inputs); if (outputPeak > 1) ++clipped; sendMidi(); return;
    }
    // Split a device block at loop boundaries. MIDI is collected once, then sliced
    // by original callback offsets so notes and note-offs are neither lost nor replayed.
    loopMidiSource.swapWith(incomingMidi); loopExternalMidi.clear();
    std::array<float*, 2> outPointers {}; std::array<const float*, 64> inPointers {};
    int offset = 0; float peak = 0;
    while (offset < n) {
        int64 frame = std::llround(cursor * sampleRate);
        if (frame < startFrame || frame >= endFrame) { frame = startFrame; cursor = frame / sampleRate; midiDiscontinuity = true; }
        cursor = frame / sampleRate;
        int count = int(jmin<int64>(n - offset, endFrame - frame));
        incomingMidi.clear(); incomingMidi.addEvents(loopMidiSource, offset, count, -offset);
        for (int ch = 0; ch < jmin(outputs, 2); ++ch) outPointers[size_t(ch)] = output[ch] ? output[ch] + offset : nullptr;
        for (int ch = 0; ch < jmin(inputs, 64); ++ch) inPointers[size_t(ch)] = input && input[ch] ? input[ch] + offset : nullptr;
        renderChunk(outPointers.data(), jmin(outputs, 2), count, inPointers.data(), jmin(inputs, 64));
        peak = jmax(peak, outputPeak.load()); loopExternalMidi.addEvents(externalMidi, 0, count, offset);
        offset += count;
        if (frame + count >= endFrame) { cursor = startFrame / sampleRate; midiDiscontinuity = true; }
    }
    incomingMidi.swapWith(loopMidiSource); loopMidiSource.clear();
    externalMidi.swapWith(loopExternalMidi); loopExternalMidi.clear();
    outputPeak = peak; if (peak > 1) ++clipped; sendMidi();
}
void Engine::renderChunk(float* const* output, int outputs, int n, const float* const* input, int inputs) {
    bool anySolo = false;
    for (auto& t : tracks) anySolo |= t->solo;
    const auto sampleRate = rate.load();
    const int64 remainingCount = countInRemaining.load();
    const int lead = recording && !exporting ? int(jmin<int64>(n, remainingCount)) : 0;
    const auto position = (recording && !exporting ? recordingPosition : cursor.load()) - lead / sampleRate;
    int playbackEnd = n;
    if (recording && !exporting && std::isfinite(recordingEnd))
        playbackEnd = jlimit(lead, n, int(jlimit(0.0, double(n), std::round((recordingEnd - position) * sampleRate))));
    if (recordingComplete && !exporting) playbackEnd = 0;
    int captureFirst = lead;
    if (recording && !tracks.empty()) {
        for (const auto& t : tracks) if (t->recorder || t->recordingMidi) {
            captureFirst = jmax(lead, int(jlimit(0.0, double(n), std::round((t->recordStart - position) * sampleRate)))); break;
        }
    }
    const bool discontinuity = midiDiscontinuity.exchange(false);
    externalMidi.clear();
    if (discontinuity && !exporting) for (int ch = 1; ch <= 16; ++ch) { externalMidi.addEvent(MidiMessage::allSoundOff(ch), 0); externalMidi.addEvent(MidiMessage::allControllersOff(ch), 0); }
    for (auto& t : tracks) {
        auto& buffer = t->work; buffer.clear(); capture.clear();
        if (recording && t->recordingMidi) captureMidi(*t, position, captureFirst, playbackEnd);
        buildClipOwners(*t, position, n, lead, playbackEnd);
        buildTrackMidi(*t, position, n, discontinuity, lead, playbackEnd);
        if (t->sendMidi && !exporting) {
            if (!t->mute && (!anySolo || t->solo)) for (const auto event : t->midiBuffer) {
                const auto m = event.getMessage();
                if (discontinuity && event.samplePosition == 0 && (m.isAllSoundOff() || (m.isController() && m.getControllerNumber() == 121))) continue;
                externalMidi.addEvent(m, event.samplePosition);
            }
        }
        const bool capturing = recording && t->recorder;
        if (capturing || (t->monitor && !exporting)) {
            for (int ch = 0; ch < 2; ++ch) {
                int source = t->input == 0 ? jmin(ch, inputs - 1) : t->input - 1;
                if (input && isPositiveAndBelow(source, inputs) && input[source]) capture.copyFrom(ch, 0, input[source], n);
            }
            const double latency = recordingLatencySamples / sampleRate;
            const int audioFirst = jmax(lead, int(jlimit(0.0, double(n), std::round((t->recordStart + latency - position) * sampleRate))));
            const int audioEnd = recordingComplete ? 0 : std::isfinite(recordingEnd)
                ? int(jlimit(0.0, double(n), std::round((recordingEnd + latency - position) * sampleRate))) : n;
            if (capturing && audioEnd > audioFirst) {
                const float* channels[] {capture.getReadPointer(0, audioFirst), capture.getReadPointer(1, audioFirst)};
                if (!t->recorder->write(channels, audioEnd - audioFirst)) dropped += audioEnd - audioFirst;
                t->recordedSamples += audioEnd - audioFirst;
                t->recordingWaveform.append(channels, audioEnd - audioFirst);
            }
            if (t->monitor && !exporting) for (int ch = 0; ch < 2; ++ch) buffer.copyFrom(ch, 0, capture, ch, 0, n);
        }
        // MON replaces this audio track's clip playback with live input. Offline
        // exports still render the clips, independent of the monitoring switch.
        const bool inputOnly = t->monitor && !t->midi && !exporting;
        if ((playing || exporting) && !inputOnly) for (int clipIndex = 0; clipIndex < int(t->clips.size()); ++clipIndex) {
            auto& c = t->clips[size_t(clipIndex)];
            if (c.midi) continue;
            if (c.start >= position + n / sampleRate || c.start + c.length <= position) continue;
            const auto& samples = c.source->samples;
            const float clipGain = Decibels::decibelsToGain(c.gainDb);
            const int first = jmax(lead, int(std::ceil((c.start - position) * sampleRate - 1e-7)));
            const int last = jmin(playbackEnd, int(std::ceil((c.start + c.length - position) * sampleRate - 1e-7)));
            for (int i = first; i < last; ++i) {
                if (t->clipOwners[size_t(i)] != clipIndex) continue;
                double local = position + i / sampleRate - c.start;
                double at = (c.offset + local) * c.source->rate;
                int a = int(at), b = jmin(a + 1, samples.getNumSamples() - 1);
                if (a < 0 || a >= samples.getNumSamples()) continue;
                float f = float(at - a), envelope = 1;
                if (c.fadeIn > 0) envelope *= float(jlimit(0.0, 1.0, local / c.fadeIn));
                if (c.fadeOut > 0) envelope *= float(jlimit(0.0, 1.0, (c.length - local) / c.fadeOut));
                for (int ch = 0; ch < 2; ++ch) {
                    auto* src = samples.getReadPointer(jmin(ch, samples.getNumChannels() - 1));
                    buffer.addSample(ch, i, (src[a] + (src[b] - src[a]) * f) * envelope * clipGain);
                }
            }
        }
        AudioBuffer<float> view(buffer.getArrayOfWritePointers(), 2, n);
        bool instrument = false;
        for (auto& p : t->inserts) instrument |= !p->bypass && p->description.isInstrument;
        if (discontinuity) t->synth.allNotesOff(0, false);
        if (t->midi && !instrument) t->synth.renderNextBlock(view, t->midiBuffer, 0, n);
        for (auto& p : t->inserts) if (!p->bypass) {
            p->instance->processBlock(view, t->midiBuffer);
        }
        float peak = 0;
        const float left = t->gain * (t->pan > 0 ? 1 - t->pan : 1);
        const float right = t->gain * (t->pan < 0 ? 1 + t->pan : 1);
        const bool audible = !t->mute && (!anySolo || t->solo);
        for (int ch = 0; ch < jmin(outputs, 2); ++ch) {
            float gain = (ch == 0 ? left : right) * (masterInserts.empty() ? master.load() : 1.0f);
            peak = jmax(peak, buffer.getMagnitude(ch, 0, n) * (ch == 0 ? left : right));
            if (audible && output[ch]) FloatVectorOperations::addWithMultiply(output[ch], buffer.getReadPointer(ch), gain, n);
        }
        t->peak = audible ? peak : 0;
    }
    if (!masterInserts.empty()) {
        masterWork.clear(); masterMidi.clear();
        for (int ch = 0; ch < jmin(outputs, 2); ++ch) if (output[ch]) masterWork.copyFrom(ch, 0, output[ch], n);
        AudioBuffer<float> view(masterWork.getArrayOfWritePointers(), 2, n);
        for (auto& p : masterInserts) if (!p->bypass) p->instance->processBlock(view, masterMidi);
        for (int ch = 0; ch < jmin(outputs, 2); ++ch) if (output[ch]) FloatVectorOperations::copyWithMultiply(output[ch], masterWork.getReadPointer(ch), master.load(), n);
    }
    // Monitor-only click: absolute transport time keeps timing independent of block size,
    // seeks and loops. Mix after recording capture and exclude offline exports.
    if ((metronome || lead > 0) && playing && !exporting) {
        const double beatsPerSecond = bpm.load() / 60.0;
        const float gain = 0.22f * master.load();
        for (int i = 0; i < n; ++i) {
            if (i >= playbackEnd || (i >= lead && !metronome)) continue;
            double clickTime = i < lead ? (countInTotal - remainingCount + i) / sampleRate : position + i / sampleRate;
            double beat = clickTime * beatsPerSecond;
            double wholeBeat = std::floor(beat + 1e-9);
            double age = jmax(0.0, (beat - wholeBeat) / beatsPerSecond);
            constexpr double length = 0.035;
            if (age >= length) continue;
            bool accent = std::fmod(wholeBeat, 4.0) == 0;
            double envelope = jmin(1.0, age / 0.001) * std::exp(-age * 100.0) * (1.0 - age / length);
            float click = gain * float(envelope * std::sin(MathConstants<double>::twoPi * (accent ? 1500 : 1000) * age));
            for (int ch = 0; ch < jmin(outputs, 2); ++ch) if (output[ch]) output[ch][i] += click;
        }
    }
    if (pitchPreviewResumeFade > 0 && !exporting) {
        const int total = jmax(1, int(rate * 0.005));
        for (int i = 0; i < n && pitchPreviewResumeFade > 0; ++i) {
            const float gain = 1.0f - float(pitchPreviewResumeFade--) / total;
            for (int ch = 0; ch < outputs; ++ch) if (output[ch]) output[ch][i] *= gain;
        }
    }
    float peak = 0;
    for (int ch = 0; ch < outputs; ++ch) if (output[ch]) {
        for (int i = 0; i < n; ++i) if (!std::isfinite(output[ch][i])) output[ch][i] = 0;
        peak = jmax(peak, FloatVectorOperations::findMinAndMax(output[ch], n).getEnd(), -FloatVectorOperations::findMinAndMax(output[ch], n).getStart());
    }
    outputPeak = peak;
    if (playing || exporting) cursor = position + playbackEnd / sampleRate;
    if (recording && !exporting) {
        if (!recordingComplete) {
            const double captureEnd = recordingEnd + recordingLatencySamples / sampleRate;
            recordingPosition = jmin(position + n / sampleRate, captureEnd);
            cursor = jmin(recordingPosition, recordingEnd);
        }
        countInRemaining = jmax<int64>(0, remainingCount - lead);
        if (remainingCount > 0 && countInRemaining == 0 && lead == n) midiDiscontinuity = true;
        if (std::isfinite(recordingEnd) && recordingPosition >= recordingEnd + recordingLatencySamples / sampleRate - 0.5 / sampleRate) {
            cursor = recordingEnd; playing = false; recordingComplete = true; midiDiscontinuity = true;
        }
    }
}
void Engine::audioDeviceIOCallbackWithContext(const float* const* input, int inputs, float* const* output, int outputs, int n, const AudioIODeviceCallbackContext&) {
    if (exporting || (deviceInterrupted && recording)) { for (int c = 0; c < outputs; ++c) if (output[c]) FloatVectorOperations::clear(output[c], n); return; }
    render(output, outputs, n, input, inputs, true);
}
void Engine::audioDeviceAboutToStart(AudioIODevice* device) {
    const ScopedLock guard(lock);
    pitchPreviewActive = false; pitchPreviewResumeFade = 0;
    rate = device->getCurrentSampleRate(); block = device->getCurrentBufferSizeSamples();
    inputLatencySamples = jmax(0, device->getInputLatencyInSamples());
    outputLatencySamples = jmax(0, device->getOutputLatencyInSamples());
    lockMisses = 0; oversizedBlocks = 0; xrunBaseline = 0;
    capture.setSize(2, jmax(8192, block)); masterWork.setSize(2, jmax(8192, block)); midiCollector.reset(rate);
    for (auto& p : masterInserts) { p->instance->releaseResources(); p->instance->setRateAndBufferSizeDetails(rate, block); p->instance->prepareToPlay(rate, block); }
    if (auto* port = devices.getDefaultMidiOutput()) port->startBackgroundThread();
    for (auto& t : tracks) prepareTrack(*t);
}
void Engine::audioDeviceStopped() { pitchPreviewActive = false; if (recording) deviceInterrupted = true; playing = false; midiDiscontinuity = true; }
void Engine::exportWav(const File& file, double start, double end, int bits) {
    exportMix(file, start, end, bits, 0);
}
void Engine::exportMp3(const File& file, double start, double end, int kbps) {
    require(kbps == 128 || kbps == 192 || kbps == 256 || kbps == 320, "Choose MP3 quality 128, 192, 256 or 320 kbps.");
    exportMix(file, start, end, 24, kbps);
}
void Engine::exportMix(const File& file, double start, double end, int bits, int kbps) {
    require(!recording, "Stop recording before exporting.");
    require(std::isfinite(start) && std::isfinite(end) && start >= 0 && end > start && end - start < 86400,
            "Choose an export range longer than zero and shorter than 24 hours.");
    stopPitchPreview({}, true); exporting = true;
    const ScopedLock guard(lock);
    const double savedCursor = cursor;
    const bool wasPlaying = playing;
    auto cleanup = [&] { for (auto& p : masterInserts) { p->instance->setNonRealtime(false); p->instance->reset(); } for (auto& t : tracks) { t->synth.allNotesOff(0, false); for (auto& p : t->inserts) { p->instance->setNonRealtime(false); p->instance->reset(); } } cursor = savedCursor; playing = wasPlaying; midiDiscontinuity = true; exporting = false; };
    File temporary = file.getSiblingFile(file.getFileName() + "." + Uuid().toString() + ".partial");
    try {
        auto stream = temporary.createOutputStream(); require(stream != nullptr, "Cannot open export destination.");
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> writer;
        std::unique_ptr<lame_global_flags, decltype(&lame_close)> encoder(kbps ? lame_init() : nullptr, lame_close);
        std::vector<unsigned char> encoded(size_t(block * 2 + 16384));
        if (kbps) {
            require(encoder != nullptr, "Cannot initialize MP3 encoder.");
            lame_set_in_samplerate(encoder.get(), int(rate.load())); lame_set_num_channels(encoder.get(), 2);
            lame_set_out_samplerate(encoder.get(), rate >= 48000 ? 48000 : 44100);
            lame_set_brate(encoder.get(), kbps); lame_set_quality(encoder.get(), 2);
            lame_set_bWriteVbrTag(encoder.get(), 1);
            lame_set_write_id3tag_automatic(encoder.get(), 0);
            require(lame_init_params(encoder.get()) >= 0, "Cannot configure MP3 encoder.");
        } else {
            writer.reset(wav.createWriterFor(stream.get(), rate, 2, bits, {}, 0));
            require(writer != nullptr, "Cannot create export writer."); stream.release();
        }
        for (auto& t : tracks) for (auto& p : t->inserts) { p->instance->reset(); p->instance->setNonRealtime(true); }
        for (auto& p : masterInserts) { p->instance->reset(); p->instance->setNonRealtime(true); }
        const auto warmFrames = jmin<int64>(int64(std::floor(start * rate)), int64(std::ceil(effectTailSeconds() * rate)));
        cursor = start - warmFrames / rate; clipped = 0; midiDiscontinuity = true;
        AudioBuffer<float> output(2, block);
        // Reconstruct the effect history before the selection, discarding preroll audio.
        // Preserve MIDI/synth state across the exact selection boundary.
        for (int64 warm = warmFrames; warm > 0;) { const int count = int(jmin<int64>(block, warm)); render(output.getArrayOfWritePointers(), 2, count); warm -= count; }
        cursor = start; clipped = 0;
        auto remaining = int64(std::llround((end - start) * rate));
        while (remaining > 0) {
            int n = int(jmin<int64>(block, remaining));
            render(output.getArrayOfWritePointers(), 2, n);
            if (kbps) {
                int bytes = lame_encode_buffer_ieee_float(encoder.get(), output.getReadPointer(0), output.getReadPointer(1), n, encoded.data(), int(encoded.size()));
                require(bytes >= 0 && stream->write(encoded.data(), size_t(bytes)), "MP3 encoding or disk write failed.");
            } else require(writer->writeFromAudioSampleBuffer(output, 0, n), "Disk write failed during export.");
            remaining -= n;
        }
        if (kbps) {
            int bytes = lame_encode_flush(encoder.get(), encoded.data(), int(encoded.size()));
            require(bytes >= 0 && stream->write(encoded.data(), size_t(bytes)), "Cannot finish MP3 encoding.");
            size_t tag = lame_get_lametag_frame(encoder.get(), encoded.data(), encoded.size());
            require(tag > 0 && tag <= encoded.size() && stream->setPosition(0) && stream->write(encoded.data(), tag), "Cannot finalize MP3 duration tag.");
            stream->flush(); require(stream->getStatus().wasOk(), "MP3 disk write failed.");
        }
        writer.reset(); stream.reset(); require(temporary.moveFileTo(file), "Cannot move exported audio to destination."); cleanup();
    } catch (...) { temporary.deleteFile(); cleanup(); throw; }
}
void Engine::reset() {
    require(!recording, "Stop recording first.");
    stop(); clearPitchLoop(); const ScopedLock guard(lock); tracks.clear(); masterInserts.clear(); cursor = 0; loop = false; metronome = false; bpm = 120;
    countInBars = 0; recordRange = false; recordFrom = 0; recordTo = 8;
    nextClipOrder = 1;
}
void Engine::saveProject(const File& file) {
    require(!recording, "Stop recording before saving.");
    const ScopedLock guard(lock);
    File media = file.getSiblingFile(file.getFileNameWithoutExtension() + "_media");
    require(media.createDirectory().wasOk(), "Cannot create project media directory.");
    auto copyAudio = [&](const std::shared_ptr<SourceAudio>& audio) {
        const auto base = String::toHexString(audio->file.getFullPathName().hashCode64()) + "_" + audio->file.getFileName();
        File target = audio->file.getParentDirectory() == media ? audio->file : media.getChildFile(base);
        if (target != audio->file && !target.existsAsFile()) require(audio->file.copyFileTo(target), "Cannot copy media: " + target.getFileName());
        return target.getRelativePathFrom(file.getParentDirectory());
    };
    var root = object(); set(root, "format", "auralis-1"); set(root, "bpm", bpm.load()); set(root, "master", master.load());
    set(root, "cursor", cursor.load()); set(root, "loopStart", loopStart.load()); set(root, "loopEnd", loopEnd.load()); set(root, "loop", loop.load());
    set(root, "metronome", metronome.load());
    set(root, "countInBars", countInBars.load()); set(root, "recordRange", recordRange.load());
    set(root, "recordFrom", recordFrom.load()); set(root, "recordTo", recordTo.load());
    Array<var> rows;
    for (auto& t : tracks) {
        var row = object(); set(row, "name", t->name); set(row, "colour", t->colour.toString()); set(row, "gain", t->gain); set(row, "pan", t->pan);
        set(row, "height", t->height); set(row, "expandedHeight", t->expandedHeight); set(row, "mute", t->mute); set(row, "solo", t->solo); set(row, "input", t->input);
        set(row, "midi", t->midi); set(row, "midiChannel", t->midiChannel); set(row, "sendMidi", t->sendMidi);
        Array<var> clips, plugins;
        for (auto& c : t->clips) {
            if (c.midi) {
                var v = object(); set(v, "midi", true); set(v, "order", c.order); set(v, "start", c.start); set(v, "offset", c.offset); set(v, "length", c.length); set(v, "name", c.name);
                Array<var> notes, events;
                for (auto& note : c.notes) { var item = object(); set(item, "beat", note.beat); set(item, "length", note.length); set(item, "pitch", note.pitch); set(item, "velocity", note.velocity); set(item, "channel", note.channel); notes.add(item); }
                for (auto& event : c.events) { var item = object(); set(item, "beat", event.beat); set(item, "data", MemoryBlock(event.message.getRawData(), size_t(event.message.getRawDataSize())).toBase64Encoding()); events.add(item); }
                set(v, "notes", notes); set(v, "events", events); clips.add(v); continue;
            }
            var v = object(); set(v, "order", c.order); set(v, "file", copyAudio(c.source)); set(v, "start", c.start);
            set(v, "offset", c.offset); set(v, "length", c.length); set(v, "fadeIn", c.fadeIn); set(v, "fadeOut", c.fadeOut); set(v, "gainDb", c.gainDb); set(v, "name", c.name);
            if (c.pitchOriginal) {
                var pitch = object(); const auto& settings = c.pitchSettings;
                set(pitch, "originalFile", copyAudio(c.pitchOriginal)); set(pitch, "root", settings.root); set(pitch, "scale", settings.scale); set(pitch, "targetNote", settings.targetNote);
                set(pitch, "referenceHz", settings.referenceHz); set(pitch, "amount", settings.amount); set(pitch, "speedMs", settings.speedMs);
                set(pitch, "maxCents", settings.maxCents); set(pitch, "preserveFormants", settings.preserveFormants); set(pitch, "manual", settings.manual);
                Array<var> edits;
                for (const auto& note : settings.notes) {
                    var item = object(); set(item, "start", note.start); set(item, "end", note.end); set(item, "original", note.original); set(item, "target", note.target);
                    if (note.scoop.enabled) {
                        var scoop = object(); set(scoop, "start", note.scoop.start); set(scoop, "depthCents", note.scoop.depthCents);
                        set(scoop, "durationMs", note.scoop.durationMs); set(scoop, "curve", note.scoop.curve); set(item, "scoop", scoop);
                    }
                    if (!note.curve.empty()) {
                        Array<var> points; for (const auto& point : note.curve) { Array<var> pair; pair.add(point.seconds); pair.add(point.note); points.add(var(pair)); }
                        set(item, "curve", points);
                    }
                    edits.add(item);
                }
                set(pitch, "noteEdits", edits); set(v, "pitchCorrection", pitch);
            }
            clips.add(v);
        }
        for (auto& p : t->inserts) {
            var v = object(); MemoryBlock state; p->instance->getStateInformation(state);
            set(v, "xml", p->description.createXml()->toString()); set(v, "state", state.toBase64Encoding()); set(v, "bypass", p->bypass); plugins.add(v);
        }
        set(row, "clips", clips); set(row, "plugins", plugins); rows.add(row);
    }
    Array<var> masterPlugins;
    for (const auto& p : masterInserts) {
        var v = object(); MemoryBlock state; p->instance->getStateInformation(state);
        set(v, "xml", p->description.createXml()->toString()); set(v, "state", state.toBase64Encoding()); set(v, "bypass", p->bypass); masterPlugins.add(v);
    }
    set(root, "masterPlugins", masterPlugins);
    set(root, "tracks", rows);
    require(file.replaceWithText(JSON::toString(root, false)), "Project could not be saved.");
}
void Engine::loadProject(const File& file) {
    require(!recording, "Stop recording before opening a project.");
    var root; auto result = JSON::parse(file.loadFileAsString(), root);
    require(result.wasOk() && root["format"].toString() == "auralis-1" && root["tracks"].isArray(), "Not an Auralis project.");
    // Build the complete project separately. A failed file/plugin load preserves the current session.
    Engine staging; staging.rate = rate.load(); staging.block = block;
    std::map<String, std::shared_ptr<SourceAudio>> cache;
    for (auto& row : *root["tracks"].getArray()) {
        auto& t = staging.addTrack(row["name"].toString(), bool(row["midi"]));
        t.height = jlimit(64, 480, int(number(row, "height", 224)));
        t.expandedHeight = jlimit(65, 480, int(number(row, "expandedHeight", t.height > 64 ? t.height : 224)));
        t.midiChannel = jlimit(0, 16, int(row["midiChannel"]));
        t.sendMidi = bool(row["sendMidi"]);
        t.colour = Colour::fromString(row["colour"].toString());
        t.gain = float(jlimit(0.0, 2.0, number(row, "gain", 0.8))); t.pan = float(jlimit(-1.0, 1.0, number(row, "pan", 0)));
        t.mute = bool(row["mute"]); t.solo = bool(row["solo"]); t.input = jlimit(0, 64, int(row["input"]));
        if (auto* list = row["clips"].getArray()) for (auto& v : *list) {
            auto restoreOrder = [&](Clip& c) {
                if (v.hasProperty("order")) c.order = int64(v["order"]); else c.order = staging.nextClipOrder++;
                require(c.order >= 0 && c.order < std::numeric_limits<int64>::max() - 1, "Invalid clip order.");
                staging.nextClipOrder = jmax(staging.nextClipOrder, c.order + 1);
            };
            if (bool(v["midi"])) {
                require(t.midi, "MIDI clip found on an audio track.");
                Clip c; c.midi = true; c.name = v["name"].toString(); c.start = double(v["start"]); c.offset = double(v["offset"]); c.length = double(v["length"]);
                require(std::isfinite(c.start) && std::isfinite(c.offset) && std::isfinite(c.length) && c.start >= 0 && c.offset >= 0 && c.length > 0, "Invalid MIDI clip range.");
                if (auto* notes = v["notes"].getArray()) for (auto& item : *notes) {
                    MidiNote note; note.beat = double(item["beat"]); note.length = double(item["length"]); note.pitch = int(item["pitch"]); note.velocity = int(item["velocity"]); note.channel = int(item["channel"]);
                    require(std::isfinite(note.beat) && std::isfinite(note.length) && note.beat >= 0 && note.length > 0 && isPositiveAndBelow(note.pitch, 128) && note.velocity >= 1 && note.velocity <= 127 && note.channel >= 1 && note.channel <= 16, "Invalid MIDI note.");
                    c.notes.push_back(note);
                }
                if (auto* events = v["events"].getArray()) for (auto& item : *events) {
                    MemoryBlock data; data.fromBase64Encoding(item["data"].toString()); double beat = double(item["beat"]);
                    require(data.getSize() > 0 && data.getSize() <= 3 && std::isfinite(beat) && beat >= 0, "Invalid MIDI controller event.");
                    MidiMessage message(data.getData(), int(data.getSize()));
                    require(message.getChannel() >= 1 && message.getChannel() <= 16 && !message.isNoteOnOrOff() && !message.isSysEx(), "Unsupported MIDI event in project.");
                    c.events.push_back({beat, message});
                }
                restoreOrder(c); t.clips.push_back(c); continue;
            }
            auto path = file.getParentDirectory().getChildFile(v["file"].toString());
            auto& source = cache[path.getFullPathName()]; if (!source) source = readAudio(path);
            Clip c; c.source = source; c.name = v["name"].toString();
            c.start = jmax(0.0, double(v["start"])); c.offset = jmax(0.0, double(v["offset"]));
            c.length = double(v["length"]); c.fadeIn = jmax(0.0, number(v, "fadeIn", 0.005)); c.fadeOut = jmax(0.0, number(v, "fadeOut", 0.005));
            double gainDb = number(v, "gainDb", 0); require(std::isfinite(gainDb), "Project contains invalid clip volume.");
            c.gainDb = float(jlimit(-60.0, 60.0, gainDb));
            require(std::isfinite(c.start) && std::isfinite(c.offset) && std::isfinite(c.length) && c.length > 0
                    && c.offset + c.length <= source->samples.getNumSamples() / source->rate + 0.01, "Project contains an invalid clip.");
            if (v.hasProperty("pitchCorrection")) {
                const auto pitch = v["pitchCorrection"];
                require(pitch.isObject() && pitch["originalFile"].toString().isNotEmpty(), "Invalid pitch correction metadata.");
                auto originalPath = file.getParentDirectory().getChildFile(pitch["originalFile"].toString());
                auto& original = cache[originalPath.getFullPathName()]; if (!original) original = readAudio(originalPath);
                require(original->rate == source->rate && original->samples.getNumSamples() == source->samples.getNumSamples()
                        && original->samples.getNumChannels() == source->samples.getNumChannels(), "Pitch correction source dimensions do not match.");
                auto& settings = c.pitchSettings; settings.root = int(number(pitch, "root", 0)); settings.scale = int(number(pitch, "scale", 0));
                settings.targetNote = int(number(pitch, "targetNote", -1)); settings.referenceHz = number(pitch, "referenceHz", 440);
                settings.amount = number(pitch, "amount", 0.75); settings.speedMs = number(pitch, "speedMs", 80); settings.maxCents = number(pitch, "maxCents", 200);
                settings.preserveFormants = !pitch.hasProperty("preserveFormants") || bool(pitch["preserveFormants"]);
                settings.manual = bool(pitch["manual"]);
                if (pitch.hasProperty("noteEdits")) {
                    require(pitch["noteEdits"].isArray(), "Invalid pitch note edits.");
                    for (const auto& note : *pitch["noteEdits"].getArray()) {
                        PitchNoteEdit edit {double(note["start"]), double(note["end"]), double(note["original"]), double(note["target"])};
                        if (note.hasProperty("scoop")) {
                            const auto scoop = note["scoop"]; require(scoop.isObject(), "Invalid pitch scoop metadata.");
                            edit.scoop = {true, number(scoop, "start", edit.start), number(scoop, "depthCents", 100), number(scoop, "durationMs", 150), number(scoop, "curve", 1)};
                        }
                        if (note.hasProperty("curve")) {
                            require(note["curve"].isArray() && note["curve"].size() <= 200000, "Invalid pitch curve.");
                            for (const auto& point : *note["curve"].getArray()) {
                                require(point.isArray() && point.size() == 2, "Invalid pitch curve point.");
                                edit.curve.push_back({double(point[0]), double(point[1])});
                            }
                        }
                        require(edit.end <= original->samples.getNumSamples() / original->rate + 0.001, "Pitch note edit exceeds source audio."); settings.notes.push_back(std::move(edit));
                    }
                }
                require(settings.valid(), "Invalid saved pitch correction settings."); c.pitchOriginal = original;
            }
            restoreOrder(c); t.clips.push_back(c);
        }
        if (auto* list = row["plugins"].getArray()) for (auto& v : *list) {
            PluginDescription desc; auto xml = parseXML(v["xml"].toString());
            require(xml && desc.loadFromXml(*xml), "Invalid plugin description in project.");
            MemoryBlock state; state.fromBase64Encoding(v["state"].toString());
            staging.rate = rate.load(); staging.block = block;
            staging.addPlugin(int(staging.tracks.size()) - 1, desc, &state); t.inserts.back()->bypass = bool(v["bypass"]);
        }
    }
    if (auto* list = root["masterPlugins"].getArray()) for (const auto& v : *list) {
        PluginDescription desc; auto xml = parseXML(v["xml"].toString()); require(xml && desc.loadFromXml(*xml), "Invalid master plugin description.");
        MemoryBlock state; state.fromBase64Encoding(v["state"].toString()); staging.addPlugin(-1, desc, &state); staging.masterInserts.back()->bypass = bool(v["bypass"]);
    }
    stop(); clearPitchLoop(); const ScopedLock guard(lock); tracks.swap(staging.tracks);
    masterInserts.swap(staging.masterInserts); for (auto& p : masterInserts) p->instance->setPlayHead(this);
    nextClipOrder = staging.nextClipOrder;
    for (auto& t : tracks) for (auto& p : t->inserts) p->instance->setPlayHead(this);
    bpm = jlimit(20.0, 300.0, number(root, "bpm", 120)); master = float(jlimit(0.0, 2.0, number(root, "master", 0.8)));
    metronome = bool(root["metronome"]);
    countInBars = jlimit(0, 2, int(number(root, "countInBars", 0))); recordRange = bool(root["recordRange"]);
    recordFrom = jmax(0.0, number(root, "recordFrom", 0)); recordTo = jmax(recordFrom + 0.01, number(root, "recordTo", 8));
    cursor = jmax(0.0, number(root, "cursor", 0)); loopStart = jmax(0.0, number(root, "loopStart", 0));
    loopEnd = jmax(loopStart + 1.0 / rate, number(root, "loopEnd", 8)); loop = bool(root["loop"]);
}
}
