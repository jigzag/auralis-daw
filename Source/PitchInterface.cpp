#include "Interface.h"
#include "PitchCorrection.h"
#include "PitchRoll.h"
#include <cmath>
#include <cstdlib>
#include <limits>

namespace auralis {
bool Workstation::installPitchCorrection(const String& trackId, const String& clipId, const Clip& before,
                                        const PitchCorrectionSettings& settings, const std::shared_ptr<SourceAudio>& processed) {
    if (busy || engine.recording || !processed || !settings.valid()) return false;
    bool updated = false;
    {
        const ScopedLock guard(engine.lock);
        for (auto& track : engine.tracks) if (track->id == trackId) for (auto& clip : track->clips) if (clip.id == clipId) {
            if (clip.midi || clip.source != before.source || clip.pitchOriginal != before.pitchOriginal || clip.offset != before.offset || clip.length != before.length || clip.pitchSettings != before.pitchSettings) return false;
            const auto original = before.pitchOriginal ? before.pitchOriginal : before.source;
            if (processed == original) {
                if (!settings.notes.empty()) { rememberEdit(); clip.source = original; clip.pitchOriginal = original; clip.pitchSettings = settings; }
                else { if (!clip.pitchOriginal) return false; rememberEdit(); clip.source = original; clip.pitchOriginal.reset(); clip.pitchSettings = {}; }
            } else {
                if (processed->rate != original->rate || processed->samples.getNumSamples() != original->samples.getNumSamples()
                    || processed->samples.getNumChannels() != original->samples.getNumChannels()) return false;
                rememberEdit(); clip.source = processed; clip.pitchOriginal = original; clip.pitchSettings = settings;
            }
            changed(); updated = true;
        }
    }
    if (updated) refresh(); return updated;
}
bool Workstation::resetPitchCorrection(const String& trackId, const String& clipId) {
    if (busy || engine.recording) return false;
    bool updated = false;
    {
        const ScopedLock guard(engine.lock);
        for (auto& track : engine.tracks) if (track->id == trackId) for (auto& clip : track->clips) if (clip.id == clipId && clip.pitchOriginal) {
            rememberEdit(); clip.source = clip.pitchOriginal; clip.pitchOriginal.reset(); clip.pitchSettings = {}; changed(); updated = true;
        }
    }
    if (updated) { refresh(); setStatus(String::fromUTF8("音程補正を解除しました。")); } return updated;
}
bool Workstation::startPitchCorrection(const String& trackId, const String& clipId, const PitchCorrectionSettings& settings, std::function<void()> after) {
    if (busy || engine.recording || engine.exporting || !settings.valid()) return false;
    if (settings.amount == 0) return resetPitchCorrection(trackId, clipId);
    Clip before; bool found = false;
    {
        const ScopedLock guard(engine.lock);
        for (const auto& track : engine.tracks) if (track->id == trackId) for (const auto& clip : track->clips)
            if (clip.id == clipId && !clip.midi && clip.source) { before = clip; found = true; }
    }
    if (!found) return false;
    engine.stop();
    const File output = dataDirectory().getChildFile("Pitch/Corrected-" + Uuid().toString() + ".wav");
    setStatus(String::fromUTF8("音程を解析・補正しています..."));
    auto result = std::make_shared<PitchCorrectionResult>();
    runBackground([before, settings, output, result] {
        *result = renderPitchCorrection(before, settings, output);
        return String::fromUTF8("音程補正が完了しました。Ctrl+Zで戻せます。");
    }, [safe = Component::SafePointer<Workstation>(this), trackId, clipId, before, settings, result, after] {
        if (!safe) return;
        if (result->correctedFrames == 0) safe->setStatus(String::fromUTF8("指定した補正範囲で音程を変更する箇所はありませんでした。"));
        const bool installed = safe->installPitchCorrection(trackId, clipId, before, settings, result->source);
        if (installed && after) after();
        else if (!installed && after) safe->setStatus(String::fromUTF8("パートが変更されたため、音程編集を適用できませんでした。再解析してください。"));
    });
    return true;
}
void Workstation::showPitchCorrectionDialog(const String& trackId, const String& clipId) {
    if (busy || engine.recording || engine.exporting) return;
    if (blockForPitchEdits()) return;
    if (pitchPrompt) { pitchPrompt->toFront(true); return; }
    const Clip* target = nullptr;
    for (const auto& track : engine.tracks) if (track->id == trackId) for (const auto& clip : track->clips)
        if (clip.id == clipId && !clip.midi && clip.source) target = &clip;
    if (!target) return;
    const auto settings = target->pitchSettings;
    auto* prompt = new AlertWindow(String::fromUTF8("音程補正"),
        String::fromUTF8("歌声・ギターの単音やメロディ向けです。コード内の音は個別に補正できません。\n元音声と長さを保持します。指定音に直す場合は、その音のパートを切り分けてください。"), MessageBoxIconType::NoIcon, this);
    prompt->setLookAndFeel(&theme); prompt->setComponentID("pitchCorrectionPrompt");
    prompt->addComboBox("root", {"C", "C# / Db", "D", "D# / Eb", "E", "F", "F# / Gb", "G", "G# / Ab", "A", "A# / Bb", "B"}, String::fromUTF8("キー"));
    prompt->getComboBoxComponent("root")->setSelectedItemIndex(settings.root, dontSendNotification);
    prompt->addComboBox("scale", {String::fromUTF8("クロマチック（最寄りの半音）"), String::fromUTF8("メジャー"), String::fromUTF8("ナチュラルマイナー")}, String::fromUTF8("スケール"));
    prompt->getComboBoxComponent("scale")->setSelectedItemIndex(settings.scale, dontSendNotification);
    StringArray targets; targets.add(String::fromUTF8("キー／スケールから自動"));
    for (int n = 24; n <= 96; ++n) targets.add(MidiMessage::getMidiNoteName(n, true, true, 4));
    prompt->addComboBox("target", targets, String::fromUTF8("補正先の音"));
    prompt->getComboBoxComponent("target")->setSelectedItemIndex(settings.targetNote < 0 ? 0 : settings.targetNote - 23, dontSendNotification);
    prompt->addTextEditor("amount", String(settings.amount * 100, 1), String::fromUTF8("補正量 (0〜100 %)"));
    prompt->addTextEditor("speed", String(settings.speedMs, 1), String::fromUTF8("追従時間 (0〜1000 ms・小さいほど強く追従)"));
    prompt->addTextEditor("limit", String(settings.maxCents, 1), String::fromUTF8("補正対象の最大ずれ (10〜1200 cent・100 cent＝半音)"));
    prompt->addTextEditor("reference", String(settings.referenceHz, 2), String::fromUTF8("基準 A4 (400〜480 Hz)"));
    prompt->addComboBox("formants", {String::fromUTF8("保持する（歌声向け）"), String::fromUTF8("保持しない")}, String::fromUTF8("声の響き（フォルマント）"));
    prompt->getComboBoxComponent("formants")->setSelectedItemIndex(settings.preserveFormants ? 0 : 1, dontSendNotification);
    prompt->addButton(String::fromUTF8("適用"), 1, KeyPress(KeyPress::returnKey));
    if (target->pitchOriginal) prompt->addButton(String::fromUTF8("解除"), 2);
    prompt->addButton(String::fromUTF8("キャンセル"), 0, KeyPress(KeyPress::escapeKey));
    pitchPrompt = prompt;
    prompt->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), prompt, trackId, clipId](int result) {
        if (!safe) return; safe->pitchPrompt = nullptr;
        if (result == 0) return;
        if (result == 2) { MessageManager::callAsync([safe, trackId, clipId] { if (safe) safe->resetPitchCorrection(trackId, clipId); }); return; }
        auto number = [&](const char* id) {
            const auto value = prompt->getTextEditorContents(id).trim(); const auto text = value.toUTF8(); char* end = nullptr;
            const double number = std::strtod(text.getAddress(), &end);
            return !value.isEmpty() && end != text.getAddress() && *end == 0 ? number : std::numeric_limits<double>::quiet_NaN();
        };
        PitchCorrectionSettings settings;
        settings.root = prompt->getComboBoxComponent("root")->getSelectedItemIndex(); settings.scale = prompt->getComboBoxComponent("scale")->getSelectedItemIndex();
        const int note = prompt->getComboBoxComponent("target")->getSelectedItemIndex(); settings.targetNote = note == 0 ? -1 : note + 23;
        settings.amount = number("amount") / 100; settings.speedMs = number("speed"); settings.maxCents = number("limit"); settings.referenceHz = number("reference");
        settings.preserveFormants = prompt->getComboBoxComponent("formants")->getSelectedItemIndex() == 0;
        if (!settings.valid()) { safe->setStatus(String::fromUTF8("音程補正の設定範囲を確認してください。")); return; }
        MessageManager::callAsync([safe, trackId, clipId, settings] { if (safe) safe->startPitchCorrection(trackId, clipId, settings); });
    }), true);
}
}
