#include "ProjectSafety.h"
#include "NativeEffects.h"
#include <cmath>
#include <stdexcept>
#include <set>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
using namespace juce;
namespace {
void demand(bool ok, const String& message) { if (!ok) throw std::runtime_error(message.toStdString()); }
bool numeric(const var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }
void numberIn(const var& object, const char* name, double low, double high) {
    if (!object.hasProperty(name)) return; const auto v = object[name];
    demand(numeric(v) && std::isfinite(double(v)) && double(v) >= low && double(v) <= high, "Invalid project number: " + String(name));
}
void localSyntax(const String& path) {
    demand(path.isNotEmpty() && path.length() <= 4096, "Invalid or overlong project path.");
    for (auto c : path) demand(c >= 32 && c != 127, "Control characters are not allowed in project paths.");
   #if JUCE_WINDOWS
    demand(path.length() >= 3 && CharacterFunctions::isLetter(path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/'), "Network, device and drive-relative project paths are blocked. Copy the project and media to a local folder.");
    demand(!path.substring(2).containsChar(':'), "Alternate data streams are not allowed in project paths.");
    demand(!path.containsAnyOf("<>|\"*?"), "Invalid characters in project path.");
    const auto parts = StringArray::fromTokens(path.substring(3).replaceCharacter('/', '\\'), "\\", "");
    for (const auto& part : parts) {
        demand(!part.endsWithChar('.') && !part.endsWithChar(' '), "Ambiguous trailing dots or spaces in project path.");
        const auto base = part.upToFirstOccurrenceOf(".", false, false).toUpperCase();
        const bool port = base.length() == 4 && (base.startsWith("COM") || base.startsWith("LPT")) && String::fromUTF8("123456789¹²³").containsChar(base[3]);
        demand(base != "CON" && base != "PRN" && base != "AUX" && base != "NUL" && base != "CONIN$" && base != "CONOUT$" && !port, "Windows device names are blocked in project paths.");
    }
    const auto drive = path.substring(0, 3).replaceCharacter('/', '\\');
    demand(GetDriveTypeW(drive.toWideCharPointer()) != DRIVE_REMOTE, "Mapped network drives are blocked for project loading. Use a local folder.");
   #else
    demand(path.startsWithChar('/') && !path.startsWith("//"), "Only local absolute project paths are supported.");
   #endif
}
File resolveMedia(const File& parent, const String& raw) {
    demand(raw.isNotEmpty() && raw.length() <= 4096, "Missing or overlong project media path.");
    demand(raw == raw.trim(), "Ambiguous leading or trailing spaces in project media path.");
    for (auto c : raw) demand(c >= 32 && c != 127, "Control characters are not allowed in media paths.");
   #if JUCE_WINDOWS
    demand(!raw.startsWithChar('\\') && !raw.startsWithChar('/'), "Network and device media paths are blocked.");
    if (raw.containsChar(':')) localSyntax(raw);
   #endif
    auto resolved = parent.getChildFile(raw); localSyntax(resolved.getFullPathName()); return resolved;
}
size_t blockSize(const String& encoded, size_t maximum) {
    if (encoded.isEmpty()) return 0;
    const int dot = encoded.indexOfChar('.'); demand(dot > 0 && dot <= 10, "Invalid saved data size prefix.");
    size_t bytes = 0;
    for (int i = 0; i < dot; ++i) { const auto c = encoded[i]; demand(c >= '0' && c <= '9', "Invalid saved data size prefix."); bytes = bytes * 10 + size_t(c - '0'); demand(bytes <= maximum, "Saved plugin or MIDI data exceeds its size limit."); }
    demand(size_t(encoded.length() - dot - 1) == (bytes * 8 + 5) / 6, "Saved data length does not match its size prefix.");
    auto payload = encoded.getCharPointer() + dot + 1;
    while (!payload.isEmpty()) { const auto c = payload.getAndAdvance(); demand((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '+', "Invalid saved data encoding."); }
    return bytes;
}
void validateJsonText(const String& text) {
    int depth = 0, tokens = 0; bool quoted = false, escaped = false;
    for (auto c : text) {
        if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; continue; }
        if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { demand(++depth <= ProjectLimits::depth, "Project JSON nesting exceeds its limit."); ++tokens; }
        else if (c == '}' || c == ']') { demand(--depth >= 0, "Invalid project JSON nesting."); }
        else if (c == ',' || c == ':') ++tokens;
        demand(tokens <= ProjectLimits::jsonTokens, "Project JSON has too many entries.");
    }
    demand(!quoted && depth == 0, "Incomplete project JSON.");
}
void finiteTree(const var& v) {
    if (v.isDouble()) demand(std::isfinite(double(v)), "Project contains a non-finite number.");
    if (auto* a = v.getArray()) for (const auto& child : *a) finiteTree(child);
    if (auto* object = v.getDynamicObject()) { const auto& properties = object->getProperties(); for (int i = 0; i < properties.size(); ++i) finiteTree(properties.getValueAt(i)); }
}
const Array<var>* arrayIn(const var& v, const char* name) {
    if (!v.hasProperty(name)) return nullptr; demand(v[name].isArray(), "Invalid project array: " + String(name)); return v[name].getArray();
}
void consume(size_t& count, size_t amount, size_t maximum, const char* what) { demand(amount <= maximum - count, "Project exceeds the limit for " + String(what) + "."); count += amount; }
}
void validateLocalProjectPath(const File& file) {
    localSyntax(file.getFullPathName());
    std::vector<File> ancestors; auto node = file;
    for (;;) { ancestors.push_back(node); const auto parent = node.getParentDirectory(); if (parent == node) break; node = parent; }
    // Inspect each ancestor before touching descendants; never traverse a junction
    // to a network target just to discover where it leads.
    for (auto i = ancestors.rbegin(); i != ancestors.rend(); ++i) {
       #if JUCE_WINDOWS
        const DWORD attributes = GetFileAttributesW(i->getFullPathName().toWideCharPointer());
        demand(attributes != INVALID_FILE_ATTRIBUTES, "Project path cannot be inspected or does not exist: " + i->getFullPathName());
        demand((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "Symbolic links, junctions and other reparse paths are blocked for project files. Copy them to a regular local folder.");
       #else
        demand(!i->isSymbolicLink(), "Symbolic links are blocked for project files.");
       #endif
    }
}
void validateProjectPluginPath(const PluginDescription& description) {
    if (isNativeEffect(description)) return;
    demand(description.pluginFormatName == "VST3", "Unsupported external plugin format in project.");
    localSyntax(description.fileOrIdentifier); File path(description.fileOrIdentifier); validateLocalProjectPath(path);
    demand(path.hasFileExtension("vst3"), "External project plugins must be VST3 files or bundles.");
    // JUCE recursively searches Windows bundles. Validate every entry before that
    // search can follow a reparse point into another folder or network share.
    std::vector<std::pair<File, int>> folders; if (path.isDirectory()) folders.emplace_back(path, 0); int entries = 0;
    while (!folders.empty()) {
        const auto [folder, depth] = folders.back(); folders.pop_back(); demand(depth <= 16, "VST3 bundle nesting exceeds its limit.");
        for (const auto& item : RangedDirectoryIterator(folder, false, "*", File::findFilesAndDirectories)) {
            demand(++entries <= 4096, "VST3 bundle has too many entries."); const auto child = item.getFile(); validateLocalProjectPath(child); if (child.isDirectory()) folders.emplace_back(child, depth + 1);
        }
    }
}
MemoryBlock decodeProjectBlock(const String& encoded, size_t maximum) {
    const auto bytes = blockSize(encoded, maximum); MemoryBlock result;
    if (encoded.isNotEmpty()) demand(result.fromBase64Encoding(encoded) && result.getSize() == bytes, "Cannot decode saved project data."); return result;
}
void ProjectDocument::requireApproval(ProjectLoadApproval approval) const {
    demand(pluginLabels.isEmpty() || approval.externalPlugins, "This project requires approval before loading external VST plugins.");
    demand(externalMedia.isEmpty() || approval.externalMedia, "This project requires approval before reading media outside its folder.");
}
std::shared_ptr<const ProjectDocument> ProjectDocument::read(const File& file) {
    validateLocalProjectPath(file);
    auto input = file.createInputStream(); demand(input && input->openedOk(), "Cannot open project.");
    demand(input->getTotalLength() > 0 && input->getTotalLength() <= ProjectLimits::jsonBytes, "Project file exceeds the 32 MiB limit or is empty.");
    MemoryBlock bytes; input->readIntoMemoryBlock(bytes, size_t(ProjectLimits::jsonBytes + 1));
    demand(bytes.getSize() > 0 && bytes.getSize() <= size_t(ProjectLimits::jsonBytes), "Project file exceeds the 32 MiB limit.");
    const auto* utf8 = static_cast<const char*>(bytes.getData()); int length = int(bytes.getSize());
    demand(memchr(utf8, 0, size_t(length)) == nullptr && CharPointer_UTF8::isValidString(utf8, length), "Project must contain valid UTF-8 JSON without NUL bytes.");
    if (length >= 3 && uint8(utf8[0]) == 0xef && uint8(utf8[1]) == 0xbb && uint8(utf8[2]) == 0xbf) { utf8 += 3; length -= 3; }
    const auto text = String::fromUTF8(utf8, length); validateJsonText(text);
    auto result = std::shared_ptr<ProjectDocument>(new ProjectDocument()); result->source = file;
    const auto parsed = JSON::parse(text, result->root); const auto& root = result->root;
    demand(parsed.wasOk() && root.isObject() && root["format"].toString() == "auralis-1" && root["tracks"].isArray(), "Not an Auralis project."); finiteTree(root);
    demand(root["tracks"].size() > 0 && root["tracks"].size() <= ProjectLimits::tracks, "Project must contain between 1 and 128 tracks.");
    for (const auto* key : {"cursor", "loopStart", "loopEnd", "recordFrom", "recordTo"}) numberIn(root, key, 0, 86400);
    numberIn(root, "bpm", 20, 300); numberIn(root, "master", 0, 2); numberIn(root, "countInBars", 0, 2);
    size_t clips = 0, notes = 0, events = 0, plugins = 0, states = 0, pitchNotes = 0, points = 0;
    std::set<String> mediaNames, pluginNames;
    auto media = [&](const var& v) {
        demand(v.isString(), "Invalid project media path."); const auto raw = v.toString(); const auto path = resolveMedia(file.getParentDirectory(), raw);
        result->media[raw] = path; if (!path.isAChildOf(file.getParentDirectory()) && mediaNames.insert(path.getFullPathName()).second) result->externalMedia.add(path.getFullPathName());
    };
    auto chain = [&](const var& owner, const char* key) {
        if (const auto* list = arrayIn(owner, key)) {
            demand(list->size() <= ProjectLimits::pluginsPerChain, "An insert chain exceeds 32 plugins."); consume(plugins, size_t(list->size()), ProjectLimits::plugins, "plugins");
            for (const auto& v : *list) {
                demand(v.isObject() && v["xml"].isString() && v["xml"].toString().getNumBytesAsUTF8() <= 16384, "Invalid or oversized plugin description.");
                const auto xmlText = v["xml"].toString(); demand(!xmlText.containsIgnoreCase("<!DOCTYPE") && !xmlText.containsIgnoreCase("<!ENTITY"), "Plugin descriptions cannot contain external XML declarations.");
                demand(xmlText.length() - xmlText.removeCharacters("<").length() <= 8, "Plugin XML nesting exceeds its limit.");
                auto xml = parseXML(xmlText); PluginDescription description; demand(xml && description.loadFromXml(*xml), "Invalid plugin description.");
                consume(states, blockSize(v["state"].toString(), ProjectLimits::stateBytes), ProjectLimits::allStateBytes, "saved plugin data");
                if (isNativeEffect(description) && v["state"].toString().isNotEmpty()) {
                    const auto nativeState = decodeProjectBlock(v["state"].toString(), 16384);
                    if (nativeState.getSize() > 0) { const auto json = String::fromUTF8(static_cast<const char*>(nativeState.getData()), int(nativeState.getSize())); validateJsonText(json); var checked; demand(JSON::parse(json, checked).wasOk(), "Invalid native plugin state."); finiteTree(checked); }
                }
                if (!isNativeEffect(description)) {
                    demand(description.pluginFormatName == "VST3", "Unsupported external plugin format."); localSyntax(description.fileOrIdentifier);
                    const auto label = description.name.substring(0, 256) + "\n" + description.fileOrIdentifier; if (pluginNames.insert(label).second) result->pluginLabels.add(label);
                }
            }
        }
    };
    for (const auto& row : *root["tracks"].getArray()) {
        demand(row.isObject() && row["name"].toString().length() <= 512, "Invalid track.");
        numberIn(row, "height", 64, 480); numberIn(row, "expandedHeight", 65, 480); numberIn(row, "input", 0, 64); numberIn(row, "midiChannel", 0, 16); numberIn(row, "gain", 0, 2); numberIn(row, "pan", -1, 1);
        if (const auto* list = arrayIn(row, "clips")) {
            consume(clips, size_t(list->size()), ProjectLimits::clips, "parts");
            for (const auto& v : *list) {
                demand(v.isObject() && v["name"].toString().length() <= 512, "Invalid part."); numberIn(v, "start", 0, 86400); numberIn(v, "offset", 0, 432000); numberIn(v, "length", 0, bool(v["midi"]) ? 432000 : 86400);
                numberIn(v, "fadeIn", 0, 86400); numberIn(v, "fadeOut", 0, 86400); numberIn(v, "gainDb", -60, 60);
                if (!bool(v["midi"])) media(v["file"]);
                if (const auto* listNotes = arrayIn(v, "notes")) { consume(notes, size_t(listNotes->size()), ProjectLimits::notes, "MIDI notes"); for (const auto& n : *listNotes) { numberIn(n, "beat", 0, 432000); numberIn(n, "length", 0, 432000); numberIn(n, "pitch", 0, 127); numberIn(n, "velocity", 1, 127); numberIn(n, "channel", 1, 16); } }
                if (const auto* listEvents = arrayIn(v, "events")) { consume(events, size_t(listEvents->size()), ProjectLimits::events, "MIDI events"); for (const auto& event : *listEvents) {
                    numberIn(event, "beat", 0, 432000); const auto data = decodeProjectBlock(event["data"].toString(), 3); demand(data.getSize() >= 2, "Incomplete MIDI event."); const auto* raw = static_cast<const uint8*>(data.getData());
                    demand(raw[0] >= 0xa0 && raw[0] <= 0xef && int(data.getSize()) == MidiMessage::getMessageLengthFromFirstByte(raw[0]), "Invalid MIDI controller event length or status.");
                    for (size_t i = 1; i < data.getSize(); ++i) demand(raw[i] < 128, "Invalid MIDI data byte.");
                } }
                if (v.hasProperty("pitchCorrection")) {
                    const auto pitch = v["pitchCorrection"]; demand(pitch.isObject(), "Invalid pitch metadata."); media(pitch["originalFile"]);
                    if (const auto* edits = arrayIn(pitch, "noteEdits")) { consume(pitchNotes, size_t(edits->size()), ProjectLimits::pitchNotes, "pitch notes"); for (const auto& edit : *edits) if (const auto* curve = arrayIn(edit, "curve")) { demand(curve->size() <= 200000, "Pitch curve exceeds its limit."); consume(points, size_t(curve->size()), ProjectLimits::curvePoints, "pitch curve points"); } }
                }
            }
        }
        chain(row, "plugins");
    }
    chain(root, "masterPlugins"); return result;
}
}
