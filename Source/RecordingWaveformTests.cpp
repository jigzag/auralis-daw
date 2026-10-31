#include "Interface.h"
#include <cmath>
#include <stdexcept>
#include <thread>
namespace auralis {
void runRecordingWaveformTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int64>::is_always_lock_free);
    {
        RecordingWaveform wave; wave.reset(); AudioBuffer<float> input(2, 600); input.clear();
        input.setSample(0, 20, -0.3f); input.setSample(1, 280, 0.8f); input.setSample(0, 590, -1.2f);
        const float* first[] {input.getReadPointer(0), input.getReadPointer(1)}; wave.append(first, 100);
        auto* storage = wave.peaks.get();
        check(wave.samples == 100 && std::abs(wave.peak(0, 100) - 0.3f) < 1e-6, "Live waveform publishes a partial bucket before a full 256 samples arrive");
        const float* rest[] {input.getReadPointer(0, 100), input.getReadPointer(1, 100)}; wave.append(rest, 500);
        check(wave.samples == 600 && wave.peaks.get() == storage && std::abs(wave.peak(0, 256) - 0.3f) < 1e-6
            && std::abs(wave.peak(256, 512) - 0.8f) < 1e-6 && std::abs(wave.peak(512, 600) - 1.2f) < 1e-6,
            "Live waveform retains both input channels and peaks across callback and bucket boundaries without reallocating");
        check(wave.peak(-100, -1) == 0 && wave.peak(600, 1000) == 0 && std::abs(wave.peak(0, 1000) - 1.2f) < 1e-6,
            "Live waveform peak queries clamp to captured data");
        wave.reset(); check(wave.samples == 0 && wave.peak(0, 1000) == 0, "A fresh recording clears the previous live waveform");
        // A long recording cannot write outside its fixed display storage.
        wave.samples = int64(RecordingWaveform::capacity) * RecordingWaveform::stride - 40;
        wave.append(first, 100);
        check(wave.samples == int64(RecordingWaveform::capacity) * RecordingWaveform::stride + 60 && wave.peak(wave.samples, wave.samples + 1000) == 0,
            "Live waveform storage remains bounded for recordings beyond its display capacity");
    }
    Workstation w(false); w.stopTimer(); auto& e = w.engine; auto& t = *e.tracks[0];
    e.recordingDirectory = directory.getChildFile("Live-waveform"); e.inputLatencySamples = 101; e.outputLatencySamples = 1667;
    t.armed = true; t.monitor = false; t.input = 2; t.gain = 0.1f; e.master = 1;
    e.recordRange = true; e.recordFrom = 0.5; e.recordTo = 2.5; e.cursor = 0; e.countInBars = 1;
    e.startRecordingWriters();
    const int64 count = e.countInRemaining, target = 24000, length = 96000, latency = 1768;
    AudioBuffer<float> input(2, 128), output(2, 128); int64 elapsed = 0; bool cleanCount = true, correctLength = true;
    while (!e.recordingComplete) {
        for (int i = 0; i < 128; ++i) {
            const int64 at = elapsed + i - count - target - latency;
            const float envelope = 0.15f + 0.65f * float(0.5 + 0.5 * std::sin(at * 0.00015));
            input.setSample(0, i, 0); input.setSample(1, i, at >= 0 && at < length ? envelope * float(std::sin(at * 0.057)) : 1);
        }
        e.render(output.getArrayOfWritePointers(), 2, 128, input.getArrayOfReadPointers(), 2);
        elapsed += 128;
        if (elapsed <= count + target + latency) cleanCount &= t.recordingWaveform.samples == 0;
        correctLength &= t.recordingWaveform.samples == t.recordedSamples;
    }
    check(cleanCount && correctLength && t.recordingWaveform.samples == length,
          "Live waveform follows latency-corrected recording samples and excludes count-in and pre-roll");
    check(t.recordingWaveform.peak(0, length) > 0.75f && !t.monitor && e.outputPeak == 0,
          "Live waveform displays the selected mono input with monitoring off and independent of track gain");
    w.setTimelineZoom(280); w.refresh();
    const auto snapshot = w.createComponentSnapshot(w.getLocalBounds());
    if (auto stream = directory.getChildFile("recording-waveform-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(snapshot, *stream);
    }
    int redWavePixels = 0; const int x0 = int(w.timeline.xAt(0.5)), x1 = int(w.timeline.xAt(2.5));
    auto local = w.timeline.createComponentSnapshot(w.timeline.getLocalBounds());
    for (int y = Timeline::ruler + 40; y < Timeline::ruler + Timeline::row - 16; ++y)
        for (int x = x0; x < x1; ++x) { auto c = local.getPixelAt(x, y); if (c.getRed() > 220 && c.getGreen() > 90 && c.getGreen() < 160 && c.getBlue() < 170) ++redWavePixels; }
    check(redWavePixels > 1500, "Native recording part paints a visible waveform rather than only a filled rectangle");
    e.finishRecording(); auto& clip = t.clips.back(); float peakError = 0;
    for (int first = 0; first < length; first += RecordingWaveform::stride) {
        int countSamples = jmin(RecordingWaveform::stride, int(length) - first);
        peakError = jmax(peakError, std::abs(t.recordingWaveform.peak(first, first + countSamples) - clip.source->samples.getMagnitude(0, first, countSamples)));
    }
    check(peakError < 2e-7f && clip.source->samples.getNumSamples() == length,
          "Live waveform peaks match the finalized raw recording without altering recorded audio");
    {
        Engine engine; auto& track = engine.addTrack(); track.monitor = true; track.gain = 1; engine.master = 1;
        track.recordingWaveform.reset(); track.recordingWaveform.samples = 48000;
        for (int i = 0; i < 188; ++i) track.recordingWaveform.peaks[i] = 0.5f;
        std::atomic<bool> running {true}; std::atomic<int> blocks {0}, bad {0}; WaitableEvent started;
        AudioBuffer<float> in(2, 128), out(2, 128); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) in.setSample(ch, i, 0.25f);
        std::thread callback([&] {
            started.signal(); while (running) {
                engine.render(out.getArrayOfWritePointers(), 2, 128, in.getArrayOfReadPointers(), 2);
                track.recordingWaveform.append(in.getArrayOfReadPointers(), 128);
                if (std::abs(out.getSample(0, 0) - 0.25f) > 1e-6) ++bad; ++blocks;
            }
        });
        started.wait(5000); float observed = 0;
        for (int repaint = 0; repaint < 100; ++repaint) for (int pixel = 0; pixel < 1000; ++pixel)
            observed = jmax(observed, track.recordingWaveform.peak(pixel * 48, pixel * 48 + 48));
        running = false; callback.join();
        check(blocks > 0 && bad == 0 && engine.lockMisses == 0 && observed >= 0.5f,
              "Concurrent live peak reading and publication do not block or corrupt audio callbacks");
    }
    directory.getChildFile("recording-waveform-verification.txt").replaceWithText("Synthetic input only. Verified partial peaks, stereo/mono selection, callback boundaries, bounded preallocated atomic storage, count-in/pre-roll and latency alignment, monitor-off visibility, native waveform painting, exact finalized audio peaks and concurrent lock-free publication/read stress.\n");
}
}
