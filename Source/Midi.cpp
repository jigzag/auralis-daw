#include "Engine.h"
#include <cmath>
#include <deque>
#include <stdexcept>

namespace auralis {
namespace {
struct ToneSound : SynthesiserSound { bool appliesToNote(int) override { return true; } bool appliesToChannel(int) override { return true; } };
struct ToneVoice : SynthesiserVoice {
    double phase = 0, step = 0; float amplitude = 0, release = 0;
    bool canPlaySound(SynthesiserSound* sound) override { return dynamic_cast<ToneSound*>(sound) != nullptr; }
    void startNote(int pitch, float velocity, SynthesiserSound*, int) override {
        phase = 0; step = MathConstants<double>::twoPi * MidiMessage::getMidiNoteInHertz(pitch) / getSampleRate(); amplitude = velocity * 0.18f; release = 1;
    }
    void stopNote(float, bool tail) override { if (tail) release = 0.99f; else { amplitude = 0; clearCurrentNote(); } }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(AudioBuffer<float>& buffer, int start, int count) override {
        if (!isVoiceActive()) return;
        for (int i = 0; i < count; ++i) {
            float value = float(std::sin(phase)) * amplitude; phase = std::fmod(phase + step, MathConstants<double>::twoPi);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.addSample(ch, start + i, value);
            if (release < 1) { amplitude *= release; if (amplitude < 0.00001f) { clearCurrentNote(); break; } }
        }
    }
};
void failIf(bool condition, String message) { if (condition) throw std::runtime_error(message.toStdString()); }

void convertEvents(Clip& clip, const std::vector<std::pair<double, MidiMessage>>& events, double endBeat) {
    std::map<int, std::deque<std::pair<double, int>>> pending;
    for (const auto& [beat, m] : events) {
        if (m.isNoteOn()) pending[m.getChannel() * 128 + m.getNoteNumber()].push_back({beat, m.getVelocity()});
        else if (m.isNoteOff()) {
            auto& queue = pending[m.getChannel() * 128 + m.getNoteNumber()];
            if (!queue.empty()) { auto on = queue.front(); queue.pop_front(); clip.notes.push_back({on.first, jmax(1.0 / 960.0, beat - on.first), m.getNoteNumber(), on.second, m.getChannel()}); }
        } else if (m.getChannel() > 0 && !m.isSysEx()) clip.events.push_back({beat, m});
    }
    for (auto& [key, queue] : pending) for (auto& on : queue) clip.notes.push_back({on.first, jmax(1.0 / 960.0, endBeat - on.first), key % 128, on.second, key / 128});
    std::stable_sort(clip.notes.begin(), clip.notes.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
}
}

void Engine::initialiseSynth(Track& t) {
    for (int i = 0; i < 16; ++i) t.synth.addVoice(new ToneVoice());
    t.synth.addSound(new ToneSound()); t.synth.setCurrentPlaybackSampleRate(rate);
}
Clip& Engine::addMidiClip(int index, double startSeconds, double lengthBeats) {
    const ScopedLock guard(lock);
    failIf(recording || !isPositiveAndBelow(index, int(tracks.size())) || !tracks[size_t(index)]->midi, "Select a MIDI track and stop recording first.");
    Clip clip; clip.midi = true; clip.name = "MIDI clip"; clip.start = jmax(0.0, startSeconds) * bpm / 60.0; clip.length = jmax(0.25, lengthBeats);
    stampNewClip(clip); tracks[size_t(index)]->clips.push_back(std::move(clip)); midiDiscontinuity = true; return tracks[size_t(index)]->clips.back();
}
void Engine::selectMidiTrack(int index) {
    const ScopedLock guard(lock);
    if (index == midiTrack) return;
    keyboard.reset(); midiTrack = index; midiDiscontinuity = true;
}
void Engine::setTempo(double value) {
    const ScopedLock guard(lock);
    failIf(recording || !std::isfinite(value), "Stop recording before changing tempo.");
    bpm = jlimit(20.0, 300.0, value); midiDiscontinuity = true;
}
bool Engine::canSplitClip(int track, int index, double seconds) const {
    if (recording || !std::isfinite(seconds) || !isPositiveAndBelow(track, int(tracks.size()))) return false;
    const auto& clips = tracks[size_t(track)]->clips;
    if (!isPositiveAndBelow(index, int(clips.size()))) return false;
    const auto& c = clips[size_t(index)]; const double at = seconds / c.scale(bpm);
    return at > c.start + 0.001 && at < c.start + c.length - 0.001;
}
void Engine::splitClip(int track, int index, double seconds) {
    const ScopedLock guard(lock);
    if (!canSplitClip(track, index, seconds)) return;
    auto& clips = tracks.at(size_t(track))->clips; auto& c = clips.at(size_t(index));
    double at = seconds / c.scale(bpm);
    Clip second = c; second.id = Uuid().toString(); second.start = at; second.offset += at - c.start; second.length -= at - c.start; c.length = at - c.start;
    clips.push_back(std::move(second)); midiDiscontinuity = true;
}
void Engine::quantizeClip(Clip& clip, double grid) {
    const ScopedLock guard(lock);
    failIf(!clip.midi || recording || grid <= 0, "Stop recording before quantizing MIDI.");
    for (auto& note : clip.notes) { note.beat = jmax(0.0, std::round(note.beat / grid) * grid); note.length = jmax(grid, std::round(note.length / grid) * grid); clip.length = jmax(clip.length, note.beat + note.length - clip.offset); }
    midiDiscontinuity = true;
}
void Engine::captureMidi(Track& t, double position, int first, int last) {
    for (const auto metadata : incomingMidi) {
        if (metadata.samplePosition < first || metadata.samplePosition >= last) continue;
        auto m = metadata.getMessage();
        if (m.getChannel() <= 0 || m.isSysEx() || (t.midiChannel > 0 && m.getChannel() != t.midiChannel)) continue;
        if (t.recordedMidi.size() == t.recordedMidi.capacity() || m.getRawDataSize() > 3) { dropped++; continue; }
        CapturedMidi event; event.seconds = jmax(0.0, position - t.recordStart + metadata.samplePosition / rate.load()); event.size = m.getRawDataSize();
        std::copy_n(m.getRawData(), event.size, event.data.begin()); t.recordedMidi.push_back(event);
    }
}
void Engine::finishMidiCapture(Track& t) {
    t.recordingMidi = false;
    if (t.recordedMidi.empty()) return;
    Clip clip; clip.midi = true; clip.name = "MIDI take"; clip.start = t.recordStart * t.recordTempo / 60.0;
    clip.length = jmax(1.0 / 960.0, (cursor - t.recordStart) * t.recordTempo / 60.0);
    std::vector<std::pair<double, MidiMessage>> events; events.reserve(t.recordedMidi.size());
    for (auto& event : t.recordedMidi) events.push_back({event.seconds * t.recordTempo / 60.0, MidiMessage(event.data.data(), event.size)});
    convertEvents(clip, events, clip.length); stampNewClip(clip); t.clips.push_back(std::move(clip)); t.recordedMidi.clear(); t.synth.allNotesOff(0, false);
}
void Engine::buildTrackMidi(Track& t, double position, int n, bool discontinuity, int first, int last) {
    if (last < 0) last = n;
    auto& midi = t.midiBuffer; midi.clear();
    auto resetChannels = [&](uint16 channels, int sample) {
        for (int ch = 1; ch <= 16; ++ch) if ((channels & (1u << (ch - 1))) != 0) {
            midi.addEvent(MidiMessage::allSoundOff(ch), sample); midi.addEvent(MidiMessage::allControllersOff(ch), sample);
        }
    };
    if (discontinuity) { resetChannels(0xffff, 0); t.playingClipId.clear(); t.playingClipChannels = 0; }
    int begin = first;
    while (begin < last) {
        int owner = t.clipOwners[size_t(begin)], end = begin + 1;
        while (end < last && t.clipOwners[size_t(end)] == owner) ++end;
        const Clip* clip = owner >= 0 && t.clips[size_t(owner)].midi ? &t.clips[size_t(owner)] : nullptr;
        String id = clip ? clip->id : String();
        bool switched = id != t.playingClipId;
        if (switched) { resetChannels(t.playingClipChannels, begin); t.playingClipChannels = 0; t.playingClipId = id; }
        if (clip) {
            const double segmentStart = position + begin / rate.load(), segmentEnd = position + end / rate.load();
            const double clipStart = clip->startTime(bpm), clipEnd = clip->endTime(bpm), secondsPerBeat = 60.0 / bpm;
            const bool chase = switched || discontinuity || first > 0;
            auto emit = [&](const MidiMessage& m, double seconds) {
                auto offset = std::llround(seconds * rate) - std::llround(position * rate);
                if (offset >= begin && offset < end) midi.addEvent(m, int(offset));
            };
            for (const auto& note : clip->notes) {
                t.playingClipChannels |= uint16(1u << (note.channel - 1));
                double on = jmax(clip->start, clip->start + note.beat - clip->offset) * secondsPerBeat;
                double off = jmin(clip->start + clip->length, clip->start + note.beat + note.length - clip->offset) * secondsPerBeat;
                if (off > on) emit(MidiMessage::noteOff(note.channel, note.pitch), off);
            }
            for (const auto& event : clip->events) {
                t.playingClipChannels |= uint16(1u << (event.message.getChannel() - 1));
                double at = jmax(clipStart, (clip->start + event.beat - clip->offset) * secondsPerBeat);
                if (at >= clipStart && at < clipEnd) {
                    if (chase && at < segmentStart && segmentStart < clipEnd) midi.addEvent(event.message, begin);
                    else if (at >= segmentStart && at < segmentEnd) emit(event.message, at);
                }
            }
            for (const auto& note : clip->notes) {
                double on = jmax(clip->start, clip->start + note.beat - clip->offset) * secondsPerBeat;
                double off = jmin(clip->start + clip->length, clip->start + note.beat + note.length - clip->offset) * secondsPerBeat;
                if (off <= on) continue;
                auto message = MidiMessage::noteOn(note.channel, note.pitch, uint8(note.velocity));
                if (chase && on < segmentStart && off > segmentStart) midi.addEvent(message, begin); else emit(message, on);
            }
        }
        begin = end;
    }
    if (last < n && !t.playingClipId.isEmpty()) {
        resetChannels(t.playingClipChannels, last); t.playingClipChannels = 0; t.playingClipId.clear();
    }
    bool live = !exporting && (t.recordingMidi || (isPositiveAndBelow(midiTrack.load(), int(tracks.size())) && tracks[size_t(midiTrack.load())].get() == &t));
    if (live) for (const auto metadata : incomingMidi) {
        auto m = metadata.getMessage();
        if (t.midiChannel == 0 || m.getChannel() == t.midiChannel) midi.addEvent(m, metadata.samplePosition);
    }
}
void Engine::importMidi(const File& file, double startSeconds) {
    failIf(recording, "Stop recording before importing MIDI.");
    auto stream = file.createInputStream(); MidiFile midi;
    failIf(!stream || file.getSize() > 16000000 || !midi.readFrom(*stream), "Cannot read MIDI file.");
    bool seconds = midi.getTimeFormat() < 0;
    if (seconds) midi.convertTimestampTicksToSeconds();
    const ScopedLock guard(lock);
    if (duration() == 0 && !seconds) {
        for (int i = 0; i < midi.getNumTracks(); ++i) for (int j = 0; j < midi.getTrack(i)->getNumEvents(); ++j) {
            auto m = midi.getTrack(i)->getEventPointer(j)->message;
            if (m.isTempoMetaEvent()) { bpm = jlimit(20.0, 300.0, 60.0 / m.getTempoSecondsPerQuarterNote()); break; }
        }
    }
    const double ticks = seconds ? 60.0 / bpm : jmax(1, int(midi.getTimeFormat()));
    for (int i = 0; i < midi.getNumTracks(); ++i) {
        std::vector<std::pair<double, MidiMessage>> events;
        String name = file.getFileNameWithoutExtension() + " " + String(i + 1); double end = 0;
        for (int j = 0; j < midi.getTrack(i)->getNumEvents(); ++j) {
            auto m = midi.getTrack(i)->getEventPointer(j)->message;
            if (m.isTrackNameEvent()) name = m.getTextFromTextMetaEvent();
            double beat = m.getTimeStamp() / ticks; end = jmax(end, beat);
            if (m.getChannel() > 0 && !m.isSysEx()) events.push_back({beat, m});
        }
        if (events.empty()) continue;
        auto& track = addTrack(name, true);
        Clip c; c.midi = true; c.name = name; c.start = jmax(0.0, startSeconds) * bpm / 60.0; c.length = jmax(1.0, end);
        convertEvents(c, events, c.length); stampNewClip(c); track.clips.push_back(std::move(c));
    }
    midiDiscontinuity = true;
}
void Engine::exportMidi(const File& file) {
    failIf(recording, "Stop recording before exporting MIDI.");
    const ScopedLock guard(lock);
    MidiFile midi; midi.setTicksPerQuarterNote(960); MidiMessageSequence tempo;
    tempo.addEvent(MidiMessage::tempoMetaEvent(int(60000000.0 / bpm))); tempo.addEvent(MidiMessage::timeSignatureMetaEvent(4, 4)); midi.addTrack(tempo);
    for (auto& t : tracks) if (t->midi) {
        MidiMessageSequence sequence;
        sequence.addEvent(MidiMessage::textMetaEvent(3, t->name));
        for (const auto& c : t->clips) if (c.midi) {
            for (const auto& note : c.notes) {
                double on = jmax(c.start, c.start + note.beat - c.offset), off = jmin(c.start + c.length, c.start + note.beat + note.length - c.offset);
                if (off <= on) continue;
                auto m = MidiMessage::noteOn(note.channel, note.pitch, uint8(note.velocity)); m.setTimeStamp(on * 960); sequence.addEvent(m);
                m = MidiMessage::noteOff(note.channel, note.pitch); m.setTimeStamp(off * 960); sequence.addEvent(m);
            }
            for (const auto& e : c.events) {
                double at = jmax(c.start, c.start + e.beat - c.offset);
                if (at >= c.start && at < c.start + c.length) { auto m = e.message; m.setTimeStamp(at * 960); sequence.addEvent(m); }
                if (e.message.isController() && e.message.getControllerNumber() == 64) { auto m = MidiMessage::controllerEvent(e.message.getChannel(), 64, 0); m.setTimeStamp((c.start + c.length) * 960); sequence.addEvent(m); }
            }
        }
        sequence.sort(); sequence.updateMatchedPairs(); midi.addTrack(sequence);
    }
    MemoryOutputStream out; failIf(!midi.writeTo(out), "MIDI encoding failed.");
    failIf(!file.replaceWithData(out.getData(), out.getDataSize()), "Cannot write MIDI file.");
}
}
