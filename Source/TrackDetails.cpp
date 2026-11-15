#include "Interface.h"
#include "Mixer.h"
#include "PluginInfo.h"
#include "Equalizer.h"
namespace auralis {
bool Workstation::toggleTrackExpanded(const String& id) {
    if (busy || engine.recording) return false;
    for (const auto& t : engine.tracks) if (t->id == id) return setTrackHeight(id, t->height <= 64 ? jlimit(65, 480, t->expandedHeight) : 64);
    return false;
}
bool Workstation::setTrackHeight(const String& id, int height) {
    if (busy || engine.recording) return false;
    for (auto& t : engine.tracks) if (t->id == id) {
        height = jlimit(64, 480, height); if (height == t->height) return true;
        if (height > 64) t->expandedHeight = height;
        t->height = height; changed(); refresh(); return true;
    } return false;
}
bool Workstation::moveTrack(const String& id, int destination) {
    if (busy || engine.recording || !isPositiveAndBelow(destination, int(engine.tracks.size()))) return false;
    {
        const ScopedLock guard(engine.lock); int source = -1;
        for (int i = 0; i < int(engine.tracks.size()); ++i) if (engine.tracks[size_t(i)]->id == id) source = i;
        if (source < 0) return false; if (source == destination) return true;
        const String selected = engine.tracks[size_t(selectedTrack)]->id;
        auto track = std::move(engine.tracks[size_t(source)]); engine.tracks.erase(engine.tracks.begin() + source); engine.tracks.insert(engine.tracks.begin() + destination, std::move(track));
        for (int i = 0; i < int(engine.tracks.size()); ++i) if (engine.tracks[size_t(i)]->id == selected) selectedTrack = i;
        engine.selectMidiTrack(selectedTrack); engine.midiDiscontinuity = true; undo.clear();
    }
    changed(); refresh(); return true;
}
bool Workstation::setTrackDetails(const String& id, const String& name, int channel, bool send) {
    if (busy || name.trim().isEmpty()) return false;
    bool modified = false;
    {
        const ScopedLock guard(engine.lock);
        Track* t = nullptr; for (auto& track : engine.tracks) if (track->id == id) { t = track.get(); break; }
        if (!t || channel < 0 || channel > (t->midi ? 16 : 32)) return false;
        if (t->name != name) { t->name = name; modified = true; }
        if (!engine.recording) { int& value = t->midi ? t->midiChannel : t->input; if (value != channel) { value = channel; modified = true; engine.midiDiscontinuity = true; } }
        if (t->midi && t->sendMidi != send) { t->sendMidi = send; modified = true; engine.midiDiscontinuity = true; }
    }
    if (modified) changed(); refresh(); return true;
}
bool Workstation::trackInsertAction(const String& trackId, const String& insertId, int action) {
    if (busy || engine.recording) return false;
    if (action == 5 && insertId.isEmpty()) {
        int index = -1;
        if (trackId.isNotEmpty()) { for (int i = 0; i < int(engine.tracks.size()); ++i) if (engine.tracks[size_t(i)]->id == trackId) index = i; if (index < 0) return false; }
        try {
            engine.addPlugin(index, equalizerDescription()); changed(); refresh();
            const auto id = index == -1 ? engine.masterInserts.back()->id : engine.tracks[size_t(index)]->inserts.back()->id;
            return trackInsertAction(trackId, id, 1);
        } catch (const std::exception& e) { error(e.what()); return false; }
    }
    if (trackId.isEmpty()) {
        for (int i = 0; i < int(engine.masterInserts.size()); ++i) if (engine.masterInserts[size_t(i)]->id == insertId) {
            auto& p = *engine.masterInserts[size_t(i)];
            if (action == 1) {
                for (auto& window : pluginWindows) if (window->getContentComponent() == p.instance->getActiveEditor()) { window->setVisible(true); window->toFront(true); return true; }
                pluginWindows.push_back(std::make_unique<PluginWindow>(*p.instance)); return true;
            }
            if (action == 2) { const ScopedLock guard(engine.lock); p.bypass = !p.bypass; }
            else if (action == 3) { pluginWindows.clear(); engine.removePlugin(-1, i); }
            else return false;
            changed(); refresh(); return true;
        } return false;
    }
    int track = -1, slot = -1;
    for (int i = 0; i < int(engine.tracks.size()); ++i) if (engine.tracks[size_t(i)]->id == trackId) {
        track = i; for (int j = 0; j < int(engine.tracks[size_t(i)]->inserts.size()); ++j) if (engine.tracks[size_t(i)]->inserts[size_t(j)]->id == insertId) { slot = j; break; } break;
    }
    if (track < 0 || slot < 0) return false;
    if (action == 1) { selectTrack(track); pluginList.selectRow(slot); showEditor(); return true; }
    if (action == 2) { const ScopedLock guard(engine.lock); auto& p = engine.tracks[size_t(track)]->inserts[size_t(slot)]; p->bypass = !p->bypass; engine.midiDiscontinuity = true; }
    else if (action == 3) { pluginWindows.clear(); engine.removePlugin(track, slot); }
    else return false;
    changed(); refresh(); return true;
}
void Workstation::trackInsertMenu(const String& trackId, const String& insertId, bool add) {
    if (busy || engine.recording) return;
    PopupMenu menu;
    auto types = knownPlugins.getTypes();
    if (trackId.isEmpty()) for (int i = types.size(); --i >= 0;) if (types[i].isInstrument) types.remove(i);
    if (add) {
        menu.addItem(5, String::fromUTF8("内蔵 EQ（音色・周波数調整）...")); menu.addSeparator();
        const auto scanned = pluginPurposeMenu(types, 100);
        for (PopupMenu::MenuItemIterator item(scanned); item.next();) menu.addItem(item.getItem());
    } else { menu.addItem(1, "Open editor", insertId.isNotEmpty()); menu.addItem(2, "Toggle bypass", insertId.isNotEmpty()); menu.addItem(3, "Remove insert", insertId.isNotEmpty()); }
    menu.addSeparator(); menu.addItem(4, "Scan VST3...");
    menu.showMenuAsync(PopupMenu::Options(), [safe = Component::SafePointer<Workstation>(this), trackId, insertId, types](int choice) {
        if (!safe || safe->busy || safe->engine.recording || choice == 0) return;
        if (choice == 4) { safe->scanPlugins(); return; }
        if (choice < 100) { safe->trackInsertAction(trackId, insertId, choice); return; }
        if (!isPositiveAndBelow(choice - 100, types.size())) return;
        if (trackId.isEmpty()) {
            try { safe->engine.addPlugin(-1, types[choice - 100]); safe->changed(); safe->refresh(); safe->trackInsertAction({}, safe->engine.masterInserts.back()->id, 1); }
            catch (const std::exception& e) { safe->error(e.what()); } return;
        }
        for (int i = 0; i < int(safe->engine.tracks.size()); ++i) if (safe->engine.tracks[size_t(i)]->id == trackId) {
            try { safe->engine.addPlugin(i, types[choice - 100]); safe->changed(); safe->refresh();
                safe->trackInsertAction(trackId, safe->engine.tracks[size_t(i)]->inserts.back()->id, 1); }
            catch (const std::exception& e) { safe->error(e.what()); } return;
        }
    });
}
}
