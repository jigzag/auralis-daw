#include "Interface.h"
#include <cmath>
#include <cstdlib>

namespace auralis {
bool Workstation::normalizeClip(const String& trackId, const String& clipId, double targetDb) {
    if (busy || engine.recording || engine.exporting || !std::isfinite(targetDb) || targetDb < -60 || targetDb > 0) return false;
    Clip original; bool found = false;
    {
        const ScopedLock guard(engine.lock);
        for (const auto& track : engine.tracks) if (track->id == trackId)
            for (const auto& clip : track->clips) if (clip.id == clipId && !clip.midi && clip.source) { original = clip; found = true; }
    }
    if (!found) return false;
    // Source audio is immutable. Scan outside the audio lock, before fades/FX,
    // using only samples in the trimmed part and one linked gain for stereo.
    const auto& source = *original.source;
    const auto& samples = source.samples;
    if (!std::isfinite(source.rate) || source.rate <= 0 || !std::isfinite(original.offset) || !std::isfinite(original.length)
        || original.offset < 0 || original.length <= 0 || samples.getNumChannels() == 0) return false;
    const int first = int(jlimit(0.0, double(samples.getNumSamples()), std::ceil(original.offset * source.rate - 1e-7)));
    const int last = int(jlimit(0.0, double(samples.getNumSamples()), std::ceil((original.offset + original.length) * source.rate - 1e-7)));
    double peak = 0;
    for (int ch = 0; ch < samples.getNumChannels(); ++ch) {
        const auto* data = samples.getReadPointer(ch);
        for (int i = first; i < last; ++i) {
            if (!std::isfinite(data[i])) { setStatus("Cannot normalize audio containing invalid samples."); return false; }
            peak = jmax(peak, std::abs(double(data[i])));
        }
    }
    if (peak == 0) { setStatus(String::fromUTF8("無音のパートはノーマライズできません。")); return false; }
    const double gain = targetDb - 20 * std::log10(peak);
    if (!std::isfinite(gain) || gain < -60 || gain > 60) {
        setStatus(String::fromUTF8("必要な音量補正が範囲（−60〜＋60 dB）を超えています。")); return false;
    }
    if (std::abs(original.gainDb - gain) < 1e-5) { setStatus(String::fromUTF8("すでに指定したピーク音量です。")); return false; }
    bool updated = false;
    {
        const ScopedLock guard(engine.lock);
        for (auto& track : engine.tracks) if (track->id == trackId) for (auto& clip : track->clips) if (clip.id == clipId) {
            if (clip.midi || clip.source != original.source || clip.offset != original.offset || clip.length != original.length) return false;
            rememberEdit(); clip.gainDb = float(gain); changed(); updated = true;
        }
    }
    if (updated) { refresh(); setStatus(String::fromUTF8("ノーマライズ: ピーク ") + String(targetDb, 2) + " dBFS. Ctrl+Z: undo."); }
    return updated;
}

void Workstation::showNormalizeDialog(const String& trackId, const String& clipId) {
    if (busy || engine.recording || engine.exporting) return;
    if (normalizePrompt) { normalizePrompt->toFront(true); return; }
    bool found = false;
    for (const auto& track : engine.tracks) if (track->id == trackId)
        for (const auto& clip : track->clips) if (clip.id == clipId && !clip.midi && clip.source) found = true;
    if (!found) return;
    auto* prompt = new AlertWindow(String::fromUTF8("ノーマライズ"),
        String::fromUTF8("トリム後の音声のピークを指定した音量に揃えます。元の音声ファイルは保持します。\n範囲: −60〜0 dBFS。初期値: −1 dBFS。"), MessageBoxIconType::NoIcon, this);
    prompt->setLookAndFeel(&theme); prompt->setComponentID("normalizePrompt");
    prompt->addTextEditor("target", "-1.0", String::fromUTF8("目標ピーク (dBFS)"));
    prompt->addButton(String::fromUTF8("適用"), 1, KeyPress(KeyPress::returnKey));
    prompt->addButton(String::fromUTF8("キャンセル"), 0, KeyPress(KeyPress::escapeKey));
    normalizePrompt = prompt;
    prompt->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), prompt, trackId, clipId](int result) {
        if (!safe) return; safe->normalizePrompt = nullptr;
        if (result != 1) return;
        const String value = prompt->getTextEditorContents("target").trim();
        const auto utf8 = value.toUTF8(); char* end = nullptr; const double db = std::strtod(utf8.getAddress(), &end);
        if (value.isEmpty() || end == utf8.getAddress() || *end != 0 || !std::isfinite(db) || db < -60 || db > 0) {
            safe->setStatus(String::fromUTF8("目標ピークには−60〜0 dBFSの数値を入力してください。")); return;
        }
        MessageManager::callAsync([safe, trackId, clipId, db] { if (safe) safe->normalizeClip(trackId, clipId, db); });
    }), true);
}
}
