#include "PluginInfo.h"
namespace auralis {
PluginUse describePlugin(const PluginDescription& p) {
    const auto category = p.category.toLowerCase(), name = p.name.toLowerCase(), maker = p.manufacturerName.toLowerCase();
    if (p.isInstrument) {
        if (category.contains("drum")) return {String::fromUTF8("ドラム音源"), String::fromUTF8("MIDIでドラムを鳴らす")};
        if (category.contains("synth")) return {String::fromUTF8("シンセ音源"), String::fromUTF8("MIDIでシンセサイザーを鳴らす")};
        return {String::fromUTF8("音源"), String::fromUTF8("MIDIノートから音を鳴らす")};
    }
    // Product roles verified against https://www.fabfilter.com/products .
    if (maker.contains("fabfilter")) {
        if (name.startsWith("pro-q")) return {"EQ", String::fromUTF8("音色・周波数バランスを調整")};
        if (name.startsWith("pro-mb")) return {String::fromUTF8("マルチバンド"), String::fromUTF8("帯域ごとに音量の強弱を整える")};
        if (name.startsWith("pro-c")) return {String::fromUTF8("コンプレッサー"), String::fromUTF8("大きい音を抑え、音量差を整える")};
        if (name.startsWith("pro-l")) return {String::fromUTF8("リミッター"), String::fromUTF8("音量のピークを抑える")};
        if (name.startsWith("pro-g")) return {String::fromUTF8("ゲート"), String::fromUTF8("小さい入力音や無音時のノイズを抑える")};
        if (name.startsWith("pro-ds")) return {String::fromUTF8("ディエッサー"), String::fromUTF8("声のサ行などの刺さる高音を抑える")};
    }
    // https://socalabs.com/effects/gate/ and https://www.meldaproduction.com/MSuperLooper .
    if (maker.contains("socalabs") && name == "gate") return {String::fromUTF8("ゲート"), String::fromUTF8("小さい入力音や無音時のノイズを抑える")};
    if (maker.contains("meldaproduction") && name.startsWith("msuperlooper")) return {String::fromUTF8("ルーパー"), String::fromUTF8("フレーズを録音して繰り返し重ねる")};
    if (category.contains("eq") || category.contains("equaliz")) return {"EQ", String::fromUTF8("音色・周波数バランスを調整")};
    if (category.contains("reverb")) return {String::fromUTF8("リバーブ"), String::fromUTF8("残響を加えて空間の響きを作る")};
    if (category.contains("delay")) return {String::fromUTF8("ディレイ"), String::fromUTF8("遅れた反復音を加える")};
    if (category.contains("filter")) return {String::fromUTF8("フィルター"), String::fromUTF8("特定の帯域を通したり削ったりする")};
    if (category.contains("distortion") || category.contains("saturat")) return {String::fromUTF8("歪み"), String::fromUTF8("歪みや倍音を加えて音の質感を変える")};
    if (category.contains("pitch")) return {String::fromUTF8("ピッチ"), String::fromUTF8("音の高さを変える")};
    if (category.contains("modulation")) return {String::fromUTF8("モジュレーション"), String::fromUTF8("揺れや広がりを加える")};
    if (category.contains("analy") || category.contains("meter")) return {String::fromUTF8("解析・メーター"), String::fromUTF8("音量や周波数などの状態を確認")};
    if (category.contains("compress")) return {String::fromUTF8("コンプレッサー"), String::fromUTF8("大きい音を抑え、音量差を整える")};
    if (category.contains("limit")) return {String::fromUTF8("リミッター"), String::fromUTF8("音量のピークを抑える")};
    if (category.contains("gate")) return {String::fromUTF8("ゲート"), String::fromUTF8("小さい入力音や無音時のノイズを抑える")};
    if (category.contains("dynamics")) return {String::fromUTF8("ダイナミクス"), String::fromUTF8("音量の強弱を調整（詳細な種類は未判定）")};
    return {String::fromUTF8("用途未判定"), String::fromUTF8("用途情報なし。製品説明で確認してください")};
}
String pluginMenuLabel(const PluginDescription& p) {
    return p.name + "  |  " + describePlugin(p).purpose + "  |  " + (p.manufacturerName.isEmpty() ? String::fromUTF8("メーカー不明") : p.manufacturerName);
}
String pluginHelp(const PluginDescription& p) {
    auto use = describePlugin(p);
    return p.name + "\n" + (p.isInstrument ? String::fromUTF8("音源 / ") : String::fromUTF8("エフェクト / ")) + use.kind + String::fromUTF8("\n用途: ") + use.purpose
        + String::fromUTF8("\nメーカー: ") + (p.manufacturerName.isEmpty() ? String::fromUTF8("不明") : p.manufacturerName) + String::fromUTF8("\n公開カテゴリ: ") + p.category;
}
PopupMenu pluginPurposeMenu(const Array<PluginDescription>& plugins, int firstId) {
    StringArray kinds;
    for (const auto& p : plugins) kinds.addIfNotAlreadyThere(describePlugin(p).kind);
    kinds.sort(true); PopupMenu menu;
    for (const auto& kind : kinds) {
        PopupMenu group;
        for (int i = 0; i < plugins.size(); ++i) if (describePlugin(plugins[i]).kind == kind) group.addItem(firstId + i, pluginMenuLabel(plugins[i]));
        menu.addSubMenu(kind, group);
    }
    return menu;
}
}
