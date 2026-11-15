#include "Interface.h"
#include "Equalizer.h"
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <winioctl.h>
#endif

namespace auralis {
namespace {
var obj(std::initializer_list<std::pair<const char*, var>> properties) { auto* result = new DynamicObject(); for (const auto& p : properties) result->setProperty(p.first, p.second); return var(result); }
var array(std::initializer_list<var> values) { Array<var> result; for (const auto& v : values) result.add(v); return var(result); }
void prop(var& v, const char* name, const var& value) { v.getDynamicObject()->setProperty(name, value); }
var project(const String& name = "Safe") { return obj({{"format", "auralis-1"}, {"tracks", array({obj({{"name", name}, {"clips", array({})}, {"plugins", array({})}})})}}); }
var& row(var& root) { return root["tracks"].getArray()->getReference(0); }
var clip(const String& file) { return obj({{"file", file}, {"name", "Synthetic"}, {"start", 0}, {"offset", 0}, {"length", 1024.0 / 48000}, {"fadeIn", 0}, {"fadeOut", 0}}); }
void pump() {
    const auto end = Time::getMillisecondCounterHiRes() + 140;
    while (Time::getMillisecondCounterHiRes() < end) {
       #if JUCE_WINDOWS
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
       #endif
        Thread::sleep(1);
    }
}
#if JUCE_WINDOWS
bool junction(const File& link, const File& target) {
    if (!link.createDirectory().wasOk()) return false;
    const String substitute = "\\??\\" + target.getFullPathName(), printable = target.getFullPathName();
    const auto a = substitute.toWideCharPointer(), b = printable.toWideCharPointer(); const WORD aBytes = WORD(substitute.length() * 2), bBytes = WORD(printable.length() * 2);
    std::vector<uint8> bytes(size_t(8 + 8 + aBytes + 2 + bBytes + 2), 0);
    auto* tag = reinterpret_cast<DWORD*>(bytes.data()); *tag = IO_REPARSE_TAG_MOUNT_POINT;
    auto* fields = reinterpret_cast<WORD*>(bytes.data() + 4); fields[0] = WORD(bytes.size() - 8); fields[1] = 0; fields[2] = 0; fields[3] = aBytes; fields[4] = aBytes + 2; fields[5] = bBytes;
    memcpy(bytes.data() + 16, a, aBytes); memcpy(bytes.data() + 16 + aBytes + 2, b, bBytes);
    HANDLE handle = CreateFileW(link.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link.getFullPathName().toWideCharPointer()); return false; }
    DWORD returned = 0; const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(), DWORD(bytes.size()), nullptr, 0, &returned, nullptr) != 0; CloseHandle(handle);
    if (!ok) RemoveDirectoryW(link.getFullPathName().toWideCharPointer()); return ok;
}
#endif
}
void runProjectSafetyTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    const auto folder = directory.getChildFile("Safety"); folder.createDirectory(); const auto file = folder.getChildFile("checked.auralis");
    auto write = [&](const var& root) { if (!file.replaceWithText(JSON::toString(root))) throw std::runtime_error("Cannot write safety fixture"); };
    Engine survivor; survivor.addTrack("Survivor"); const auto survivorId = survivor.tracks[0]->id;
    auto rejects = [&](const var& root, const String& expected) {
        write(root); String message; try { survivor.loadProject(file, {true, true}); } catch (const std::exception& e) { message = e.what(); }
        return message.containsIgnoreCase(expected) && survivor.tracks.size() == 1 && survivor.tracks[0]->id == survivorId;
    };
    write(project()); const auto safe = ProjectDocument::read(file); check(safe->pluginsToApprove().isEmpty() && safe->mediaToApprove().isEmpty(), "Ordinary local projects need no extra approval");
    Engine empty; empty.loadProject(*safe); check(empty.tracks.size() == 1 && empty.tracks[0]->name == "Safe", "Validated local project documents load through the safe default API");
    write(project("Disk changed")); Engine snapshot; snapshot.loadProject(*safe); check(snapshot.tracks[0]->name == "Safe", "Opening uses the inspected immutable snapshot rather than rereading changed project bytes");
    { auto stream = file.createOutputStream(); stream->setPosition(0); stream->truncate(); const uint8 bom[] {0xef, 0xbb, 0xbf}; stream->write(bom, 3); stream->writeText(JSON::toString(project()), false, false, nullptr); }
    check(ProjectDocument::read(file)->pluginsToApprove().isEmpty(), "UTF-8 BOM projects remain compatible");
    auto wav = [&](const File& destination) { auto stream = destination.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat format; std::unique_ptr<AudioFormatWriter> writer(format.createWriterFor(stream.get(), 48000, 2, 24, {}, 0)); stream.release(); AudioBuffer<float> b(2, 1024); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 1024; ++i) b.setSample(ch, i, 0.25f); if (!writer || !writer->writeFromAudioSampleBuffer(b, 0, 1024)) throw std::runtime_error("Safety WAV write failed"); };
    const auto local = folder.getChildFile("local.wav"), outside = directory.getChildFile("outside-safety.wav"); wav(local); wav(outside);
    auto mediaProject = project(); prop(row(mediaProject), "clips", array({clip("local.wav")})); write(mediaProject); const auto contained = ProjectDocument::read(file); Engine loaded; loaded.loadProject(*contained);
    check(contained->mediaToApprove().isEmpty() && loaded.tracks[0]->clips[0].source->samples.getSample(0, 0) == 0.25f, "Contained media loads normally without granting external file access");
    prop(row(mediaProject), "clips", array({clip("../outside-safety.wav")})); write(mediaProject); const auto external = ProjectDocument::read(file); bool blocked = false;
    try { survivor.loadProject(*external); } catch (const std::exception& e) { blocked = String(e.what()).containsIgnoreCase("approval"); }
    check(blocked && external->mediaToApprove().contains(outside.getFullPathName()) && survivor.tracks[0]->id == survivorId, "Parent traversal becomes an explicit external-media request before file decoding");
    loaded.loadProject(*external, {false, true}); check(loaded.tracks[0]->clips[0].source->file == outside, "Explicitly approved external local media remains compatible");
    const auto migrated = folder.getChildFile("contained-save.auralis"); loaded.saveProject(migrated); check(ProjectDocument::read(migrated)->mediaToApprove().isEmpty(), "Saving copies approved external media into the project media folder");
    const StringArray paths {"\\\\blocked.invalid\\share\\audio.wav", "//blocked.invalid/share/audio.wav", "\\\\?\\C:\\audio.wav", "\\\\.\\pipe\\audio.wav", "C:audio.wav", "local.wav:alternate", "\\rooted.wav", "NUL.wav", "COM1.txt", "LPT1", "local.wav.", "local.wav "};
    for (const auto& path : paths) { auto root = project(); prop(row(root), "clips", array({clip(path)})); String message; write(root); try { auto document = ProjectDocument::read(file); survivor.loadProject(*document, {true, true}); } catch (const std::exception& e) { message = e.what(); }
        check(message.isNotEmpty() && survivor.tracks[0]->id == survivorId, "Unsafe media path is blocked without network/device access: " + path); }
    auto pitch = project(); auto part = clip("local.wav"); prop(part, "pitchCorrection", obj({{"originalFile", "\\\\blocked.invalid\\share\\original.wav"}})); prop(row(pitch), "clips", array({part})); check(rejects(pitch, "blocked"), "Pitch original-file paths cannot bypass the media path policy");
    OwnedArray<PluginDescription> descriptions; const auto fixture = File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("TestGain.vst3");
    // Only our locally built fixture is scanned/executed; no malicious module is used.
    for (auto* format : loaded.pluginFormats.getFormats()) format->findAllTypesForFile(descriptions, fixture.getFullPathName()); check(descriptions.size() == 1, "Safety tests use a known local TestGain fixture");
    auto plugin = [&](const PluginDescription& desc, const String& state = "0.") { return obj({{"xml", desc.createXml()->toString()}, {"state", state}, {"bypass", true}}); };
    auto pluginProject = project("Approved fixture"); prop(row(pluginProject), "plugins", array({plugin(*descriptions[0])})); write(pluginProject); const auto untrusted = ProjectDocument::read(file); blocked = false;
    try { survivor.loadProject(*untrusted); } catch (const std::exception& e) { blocked = String(e.what()).containsIgnoreCase("approval"); }
    check(blocked && untrusted->pluginsToApprove().size() == 1 && survivor.tracks[0]->id == survivorId, "Even bypassed external VSTs are denied by default before processor creation");
    loaded.loadProject(*untrusted, {true, false}); check(loaded.tracks[0]->inserts.size() == 1 && loaded.tracks[0]->inserts[0]->bypass, "Explicit approval restores external VST state and insert bypass");
    auto master = project(); prop(master, "masterPlugins", array({plugin(*descriptions[0])})); write(master); const auto masterDocument = ProjectDocument::read(file); blocked = false;
    try { survivor.loadProject(*masterDocument); } catch (...) { blocked = true; } check(blocked && masterDocument->pluginsToApprove().size() == 1, "Master VSTs receive the same default-deny project trust policy");
    auto remotePlugin = *descriptions[0]; remotePlugin.fileOrIdentifier = "\\\\blocked.invalid\\share\\effect.vst3"; auto remote = project(); prop(row(remote), "plugins", array({plugin(remotePlugin)})); check(rejects(remote, "blocked"), "Network VST paths are blocked even with an approval flag");
    auto native = project(); prop(row(native), "plugins", array({plugin(equalizerDescription())})); write(native); loaded.loadProject(file); check(loaded.tracks[0]->inserts.size() == 1 && ProjectDocument::read(file)->pluginsToApprove().isEmpty(), "Built-in processors remain usable without external-code approval");
    for (const auto& encoded : StringArray {"-1.", "2147483647.", "8388609.", "1.", "2.A", "1.!A"}) { auto root = project(); prop(row(root), "plugins", array({plugin(*descriptions[0], encoded)})); check(rejects(root, "data") || rejects(root, "size"), "Forged saved-state size prefixes are rejected before allocation: " + encoded); }
    MemoryBlock largeState(262144, true); memset(largeState.getData(), 0x5a, largeState.getSize()); check(decodeProjectBlock(largeState.toBase64Encoding(), ProjectLimits::stateBytes) == largeState, "Large valid plugin states decode with a single linear validation pass");
    auto nestedXml = project(); prop(row(nestedXml), "plugins", array({obj({{"xml", String::repeatedString("<PLUGIN>", 12) + String::repeatedString("</PLUGIN>", 12)}, {"state", "0."}})})); check(rejects(nestedXml, "XML nesting"), "Nested plugin XML is rejected before the recursive XML parser");
    auto nestedState = String::repeatedString("[", 60) + "0" + String::repeatedString("]", 60); MemoryBlock nestedBytes(nestedState.toRawUTF8(), size_t(nestedState.getNumBytesAsUTF8()));
    auto nativeNested = project(); prop(row(nativeNested), "plugins", array({plugin(equalizerDescription(), nestedBytes.toBase64Encoding())})); check(rejects(nativeNested, "nesting"), "Nested native plugin state is bounded before the processor's JSON parser");
    Array<var> tooMany; tooMany.insertMultiple(-1, var(), ProjectLimits::tracks + 1); auto many = project(); prop(many, "tracks", var(tooMany)); check(rejects(many, "128 tracks"), "Track limits are checked before creating tracks or audio buffers");
    tooMany.clear(); tooMany.insertMultiple(-1, var(), ProjectLimits::pluginsPerChain + 1); many = project(); prop(row(many), "plugins", var(tooMany)); check(rejects(many, "32 plugins"), "Insert-chain counts are bounded before decoding or loading plugins");
    tooMany.clear(); tooMany.insertMultiple(-1, var(), ProjectLimits::clips + 1); many = project(); prop(row(many), "clips", var(tooMany)); check(rejects(many, "parts"), "Aggregate clip limits are checked before audio decoding");
    tooMany.clear(); tooMany.insertMultiple(-1, var(), ProjectLimits::notes + 1); many = project(); part = obj({{"midi", true}, {"start", 0}, {"offset", 0}, {"length", 4}, {"notes", var(tooMany)}}); prop(row(many), "midi", true); prop(row(many), "clips", array({part})); check(rejects(many, "MIDI notes"), "MIDI note counts are bounded before allocating note collections");
    tooMany.clear(); tooMany.insertMultiple(-1, var(), ProjectLimits::events + 1); prop(part, "notes", array({})); prop(part, "events", var(tooMany)); prop(row(many), "clips", array({part})); check(rejects(many, "MIDI events"), "MIDI event counts are bounded before decoding event data");
    tooMany.clear(); many = project(); prop(many, "cursor", "1e309"); check(rejects(many, "number"), "Non-numeric or overflowing transport positions cannot reach render-time integer conversions");
    many = project(); prop(row(many), "height", 1e100); check(rejects(many, "number"), "Oversized track dimensions are rejected before integer casts");
    MemoryBlock badMidi; const uint8 status = 0xb0; badMidi.append(&status, 1); many = project(); part = obj({{"midi", true}, {"length", 4}, {"events", array({obj({{"beat", 0}, {"data", badMidi.toBase64Encoding()}})})}}); prop(row(many), "midi", true); prop(row(many), "clips", array({part})); check(rejects(many, "MIDI"), "Truncated MIDI messages are rejected before MidiMessage construction");
    const auto nested = String("{\"format\":\"auralis-1\",\"tracks\":[],\"deep\":") + String::repeatedString("[", 60) + "0" + String::repeatedString("]", 60) + "}"; file.replaceWithText(nested); blocked = false;
    try { ProjectDocument::read(file); } catch (const std::exception& e) { blocked = String(e.what()).containsIgnoreCase("nesting"); } check(blocked, "JSON nesting is limited before invoking the recursive JSON parser");
    file.replaceWithText("[" + String::repeatedString("0,", ProjectLimits::jsonTokens + 1) + "0]"); blocked = false;
    try { ProjectDocument::read(file); } catch (const std::exception& e) { blocked = String(e.what()).contains("too many entries"); } check(blocked, "JSON token budgets are enforced before allocating the parsed tree");
    { auto stream = file.createOutputStream(); stream->setPosition(ProjectLimits::jsonBytes); stream->writeByte('x'); stream->truncate(); } blocked = false;
    try { ProjectDocument::read(file); } catch (const std::exception& e) { blocked = String(e.what()).contains("32 MiB"); } check(blocked, "Oversized project files are rejected before reading their contents");
    int64 budget = 2 * 1024 * 4 + ((1024 + 63) / 64) * 8 + 4096; const auto cost = budget; auto audio = loaded.readAudio(local, &budget); blocked = false;
    try { loaded.readAudio(outside, &budget); } catch (const std::exception& e) { blocked = String(e.what()).contains("memory budget"); }
    check(audio && budget == 0 && cost < 20000 && blocked, "Decoded audio and waveform allocations share an aggregate budget checked before buffer allocation");
   #if JUCE_WINDOWS
    const auto link = folder.getChildFile("linked-media"); check(link.isAChildOf(folder) && junction(link, directory), "Local junction fixture is created without following a remote target");
    auto linked = project(); prop(row(linked), "clips", array({clip("linked-media/outside-safety.wav")})); check(rejects(linked, "reparse"), "A contained path through a junction cannot bypass the external-media boundary");
    check(RemoveDirectoryW(link.getFullPathName().toWideCharPointer()) != 0, "Junction cleanup removes only the link and keeps its target intact");
    const auto bundle = folder.getChildFile("linked.vst3"); bundle.createDirectory(); const auto bundleLink = bundle.getChildFile("Contents"); check(junction(bundleLink, directory), "VST bundle junction fixture is created locally");
    auto linkedDescription = *descriptions[0]; linkedDescription.fileOrIdentifier = bundle.getFullPathName(); blocked = false; try { validateProjectPluginPath(linkedDescription); } catch (const std::exception& e) { blocked = String(e.what()).containsIgnoreCase("reparse"); }
    check(blocked, "VST bundle traversal rejects junctions before JUCE's recursive module search"); check(RemoveDirectoryW(bundleLink.getFullPathName().toWideCharPointer()) != 0, "VST bundle junction cleanup preserves the target directory");
   #endif
    Workstation w(false); w.stopTimer(); w.engine.tracks[0]->name = "Current session"; const auto originalId = w.engine.tracks[0]->id;
    write(pluginProject); w.loadProjectFile(file); auto* prompt = w.projectTrustPrompt.getComponent(); check(prompt && prompt->isCurrentlyModal() && w.engine.tracks[0]->id == originalId && w.engine.tracks[0]->inserts.empty(), "The project trust dialog appears before any external plugin is loaded"); prompt->setAlpha(0); pump();
    auto* requirements = dynamic_cast<TextEditor*>(prompt->findChildWithID("projectRequirements")); check(requirements && requirements->isReadOnly() && requirements->getPasswordCharacter() == 0 && requirements->isMultiLine() && requirements->getHeight() >= 160 && requirements->getText().contains(fixture.getFullPathName()), "The trust dialog displays the unmasked plugin path in a multiline read-only list");
    if (auto stream = directory.getChildFile("project-trust-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(prompt->createComponentSnapshot(prompt->getLocalBounds()), *stream); }
    prompt->triggerButtonClick(String::fromUTF8("キャンセル")); pump(); check(!w.projectTrustPrompt && w.engine.tracks[0]->id == originalId && w.projectFile == File(), "Cancelling trust approval preserves the current session and selected project");
    // Changing the file while the dialog is open must not replace the approved snapshot.
    write(pluginProject); w.loadProjectFile(file); prompt = w.projectTrustPrompt.getComponent(); prompt->setAlpha(0); write(remote); prompt->triggerButtonClick(String::fromUTF8("許可して開く")); pump();
    check(!w.projectTrustPrompt && w.engine.tracks[0]->name == "Approved fixture" && w.engine.tracks[0]->inserts.size() == 1, "Approval loads the inspected snapshot even if the on-disk project is replaced while prompting");
    write(mediaProject); w.loadProjectFile(file); prompt = w.projectTrustPrompt.getComponent(); requirements = prompt ? dynamic_cast<TextEditor*>(prompt->findChildWithID("projectRequirements")) : nullptr; check(requirements && requirements->getText().contains(outside.getFullPathName()), "External-media approval shows the resolved absolute path"); prompt->setAlpha(0); prompt->triggerButtonClick(String::fromUTF8("許可して開く")); pump();
    check(!w.projectTrustPrompt && w.engine.tracks[0]->clips.size() == 1 && w.engine.tracks[0]->clips[0].source->file == outside, "Explicit media approval opens the intended external local file");
    check(!survivor.devices.getCurrentAudioDevice() && !loaded.devices.getCurrentAudioDevice() && !w.engine.devices.getCurrentAudioDevice(), "Security tests never open hardware or access a remote share");
    directory.getChildFile("project-safety-verification.txt").replaceWithText("Offline/local fixtures only. Default-deny track/master VST loading, bypass does not grant execution, explicit UI approval and cancellation, immutable inspected snapshot, built-in compatibility, external-media approval/copy-on-save, UNC/device/ADS/reserved-path blocking, native and VST-bundle junction rejection, bounded JSON bytes/depth/counts, validated size-prefixed state decoding, native-state JSON nesting, MIDI message bounds, finite numeric ranges, aggregate decoded audio/waveform budget, atomic session preservation. No malicious plugin or remote share executed.\n");
}
}
