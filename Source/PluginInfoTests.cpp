#include "PluginInfo.h"
#include <set>
#include <stdexcept>
namespace auralis {
void runPluginInfoTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto plugin = [](String name, String maker, String category, bool instrument = false) { PluginDescription p; p.name = name; p.manufacturerName = maker; p.category = category; p.isInstrument = instrument; return p; };
    Array<PluginDescription> types;
    for (auto item : {std::pair<const char*, const char*>{"Pro-Q 4", "EQ"}, {"Pro-C 3", "コンプレッサー"}, {"Pro-L 2", "リミッター"}, {"Pro-MB", "マルチバンド"}, {"Pro-G", "ゲート"}, {"Pro-DS", "ディエッサー"}}) {
        auto p = plugin(item.first, "FabFilter", "Fx|Dynamics");
        check(describePlugin(p).kind == String::fromUTF8(item.second), "Verified product role distinguishes FabFilter " + p.name + " despite broad scan category"); types.add(p);
    }
    auto drums = plugin("MODO DRUM", "IK Multimedia", "Instrument|Drum", true); types.add(drums);
    check(describePlugin(drums).kind == String::fromUTF8("ドラム音源") && pluginHelp(drums).contains("MIDI"), "Instrument metadata is described as a sound source with a plain purpose");
    auto reverb = plugin("OldSkoolVerb", "Voxengo", "Fx|Reverb"); types.add(reverb);
    check(describePlugin(reverb).kind == String::fromUTF8("リバーブ") && pluginMenuLabel(reverb).contains(String::fromUTF8("残響")) && pluginMenuLabel(reverb).contains("Voxengo"), "Category-based menu labels include purpose and manufacturer");
    auto gate = plugin("Gate", "SocaLabs", "Fx"), looper = plugin("MSuperLooper16out", "MeldaProduction", "Fx"); types.add(gate); types.add(looper);
    check(describePlugin(gate).kind == String::fromUTF8("ゲート") && describePlugin(looper).kind == String::fromUTF8("ルーパー"), "Verified Gate and looper roles are available when scan metadata only says Fx");
    auto unknown = plugin("Mystery", "", "Fx"); types.add(unknown);
    check(describePlugin(unknown).kind == String::fromUTF8("用途未判定") && pluginMenuLabel(unknown).contains(String::fromUTF8("メーカー不明")), "Missing purpose and manufacturer metadata remain explicitly unknown");
    check(describePlugin(plugin("Pro-C 3", "Other maker", "Fx|Dynamics")).kind == String::fromUTF8("ダイナミクス"), "Product-specific explanations require the matching manufacturer");
    auto menu = pluginPurposeMenu(types, 100); std::set<int> ids; bool descriptions = true;
    for (PopupMenu::MenuItemIterator iterator(menu, true); iterator.next();) { auto& item = iterator.getItem(); if (item.itemID >= 100) {
        ids.insert(item.itemID); int index = item.itemID - 100; descriptions &= isPositiveAndBelow(index, types.size()) && item.text == pluginMenuLabel(types[index]);
    } }
    check(ids.size() == size_t(types.size()) && descriptions, "Grouped plugin menus retain exact stable index mapping and show explanations for every plugin");
    PluginDescription restored; auto xml = reverb.createXml(); restored.loadFromXml(*xml);
    check(pluginHelp(restored) == pluginHelp(reverb) && pluginHelp(restored).contains(String::fromUTF8("公開カテゴリ")), "Restored scan descriptions retain category, manufacturer and detailed tooltip content");
    String report = "Verified Japanese purpose labels, manufacturer-qualified product mappings, category fallbacks, instruments, explicit unknowns, menu item ID integrity and restored metadata.\n";
    for (const auto& p : types) report += pluginMenuLabel(p) + "\n";
    directory.getChildFile("plugin-purpose-verification.txt").replaceWithText(report);
}
}
