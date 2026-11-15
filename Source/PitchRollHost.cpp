#include "PitchRoll.h"

namespace auralis {
bool Workstation::blockForPitchEdits() {
    for (const auto& window : pitchWindows) if (window->panel().hasPendingEdits()) {
        window->panel().showCommitRequired();
        window->setVisible(true); window->toFront(true);
        setStatus(String::fromUTF8("音程エディターに未適用の変更があります。「適用」または「変更を破棄」を選んでください。")); return true;
    }
    return false;
}
void Workstation::showPitchRoll(const String& trackId, const String& clipId, bool reanalyse) {
    if (busy || engine.recording || engine.exporting) return;
    for (const auto& window : pitchWindows) if (window->clipId == clipId) {
        if (!reanalyse || window->panel().hasPendingEdits()) { window->setVisible(true); window->toFront(true); return; }
    }
    Clip before; bool found = false;
    for (const auto& track : engine.tracks) if (track->id == trackId) for (const auto& clip : track->clips)
        if (clip.id == clipId && !clip.midi && clip.source) { before = clip; found = true; }
    if (!found) return;
    engine.stop(); setStatus(String::fromUTF8("音程エディターのために音を解析しています..."));
    auto data = std::make_shared<std::vector<PitchFrame>>();
    runBackground([before, data] {
        auto settings = before.pitchSettings; if (!before.pitchOriginal) { settings.manual = true; settings.amount = 1; settings.speedMs = 0; }
        *data = analysePitch(before, settings); return String::fromUTF8("音程エディターを開きました。");
    }, [safe = Component::SafePointer<Workstation>(this), trackId, clipId, before, data] {
        if (!safe) return;
        bool current = false;
        for (const auto& track : safe->engine.tracks) if (track->id == trackId) for (const auto& clip : track->clips)
            if (clip.id == clipId && clip.source == before.source && clip.offset == before.offset && clip.length == before.length) current = true;
        if (!current) return;
        for (const auto& window : safe->pitchWindows) if (window->clipId == clipId) {
            window->panel().setAnalysis(before, *data); window->setVisible(true); window->toFront(true); return;
        }
        safe->pitchWindows.push_back(std::make_unique<PitchRollWindow>(*safe, trackId, clipId, before, *data));
    });
}
}
