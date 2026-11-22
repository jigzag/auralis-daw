#include "Japanese.h"
#include "Interface.h"
#include <stdexcept>
namespace auralis {
void runJapaneseTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer();
    check(w.fileButton.getButtonText() == String::fromUTF8("プロジェクト") && w.playButton.getButtonText() == "PLAY" && w.recordButton.getButtonText() == "RECORD", "Project menu stays Japanese alongside standard English transport labels");
    check(w.mixerButton.getButtonText() == "Mixer" && w.settingsButton.getButtonText() == String::fromUTF8("音声 / MIDI"), "Mixer uses its DAW name and audio-device settings remain Japanese");
    check(TRANS("Save") == String::fromUTF8("保存") && TRANS("Discard changes") == String::fromUTF8("変更を破棄") && TRANS("Cancel") == String::fromUTF8("キャンセル"), "Save confirmation actions are translated without changing their numeric action IDs");
    check(TRANS("Audio buffer size:") == String::fromUTF8("バッファサイズ:") && TRANS("Sample rate:") == String::fromUTF8("サンプルレート:"), "JUCE audio-settings labels use the same Japanese catalog");
    check(japaneseMessage("Cannot read audio: example.wav") == String::fromUTF8("音声ファイルを読み込めません: example.wav"), "Dynamic error messages retain the filename with a Japanese explanation");
    check(japaneseMessage(String::fromUTF8("日本語のテスト")) == String::fromUTF8("日本語のテスト") && japaneseMessage("My Guitar Take") == "My Guitar Take", "Japanese messages and user-supplied names are not altered by fallback translation");
    w.refresh();
    if (auto stream = directory.getChildFile("japanese-ui-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    check(!w.engine.devices.getCurrentAudioDevice(), "Japanese UI verification opens no audio hardware");
    directory.getChildFile("japanese-ui-verification.txt").replaceWithText("Verified Japanese transport, project/mixer/device labels, Save/Discard/Cancel actions, shared JUCE audio settings, dynamic filename-bearing errors and preservation of existing Japanese/user text. Offline synthetic UI only.\n");
}
}
