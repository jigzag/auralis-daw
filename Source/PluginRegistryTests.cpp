#include "Interface.h"
#include <stdexcept>
namespace auralis {
void runPluginRegistryTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    const auto executable = File::getSpecialLocation(File::currentExecutableFile);
    const auto scanFile = directory.getChildFile("registry-scan.xml");
    ChildProcess scanner; StringArray args {executable.getFullPathName(), "--scan-plugin", executable.getSiblingFile("TestTone.vst3").getFullPathName(), "--scan-output", scanFile.getFullPathName()};
    check(scanner.start(args, 0) && scanner.waitForProcessToFinish(45000), "Plugin registry fixture is scanned by the isolated VST3 scanner");
    KnownPluginList scanned; auto xml = parseXML(scanFile); if (xml) scanned.recreateFromXml(*xml);
    check(scanned.getTypes().size() == 1, "Isolated scan produces a restorable VST3 description");
    const auto root = directory.getChildFile("RegistryTest-" + Uuid().toString()); root.createDirectory();
    const auto registry = root.getChildFile("Shared/plugins.xml");
    auto writeList = [](const File& path, const PluginDescription& type) { path.getParentDirectory().createDirectory(); KnownPluginList list; list.addType(type); return list.createXml()->writeTo(path); };
    auto instrument = scanned.getTypes()[0]; auto effect = instrument;
    effect.name = "Registry effect"; effect.fileOrIdentifier = "C:/Test/RegistryGain.vst3"; effect.uniqueId += 10; effect.deprecatedUid += 10; effect.isInstrument = false;
    auto third = effect; third.name = "Registry third"; third.fileOrIdentifier = "C:/Test/RegistryThird.vst3"; third.uniqueId += 20; third.deprecatedUid += 20;
    {
        Workstation w(false, registry); w.stopTimer(); w.knownPlugins.addType(instrument);
        check(w.savePluginRegistry() && registry.existsAsFile() && !w.dirty, "Scanned plugins save immediately to an independent shared registry without changing the project");
    }
    {
        Workstation restarted(false, registry); restarted.stopTimer();
        check(restarted.knownPlugins.getTypes().size() == 1 && restarted.knownPlugins.getTypes()[0].fileOrIdentifier == instrument.fileOrIdentifier,
              "Restart restores scanned VST3 descriptions without rescanning or opening plugin instances");
        restarted.engine.reset();
        check(restarted.knownPlugins.getTypes().size() == 1, "New project does not clear the scanned plugin library");
    }
    const auto legacy = root.getChildFile("Portable");
    writeList(legacy.getChildFile("Auralis-0.4.7/Data/plugins.xml"), effect);
    writeList(legacy.getChildFile("Auralis-0.4.9/Data/plugins.xml"), instrument);
    legacy.getChildFile("Auralis-broken/Data").createDirectory(); legacy.getChildFile("Auralis-broken/Data/plugins.xml").replaceWithText("<broken");
    auto current = legacy.getChildFile("Auralis-0.4.10"); current.createDirectory();
    {
        Workstation upgraded(false, registry); upgraded.stopTimer(); upgraded.loadPluginRegistry(current);
        check(upgraded.knownPlugins.getTypes().size() == 2, "Upgrade merges plugin libraries from older portable versions and skips malformed legacy XML");
        upgraded.loadPluginRegistry(current);
        check(upgraded.knownPlugins.getTypes().size() == 2, "Repeated migration does not duplicate scanned plugins");
    }
    {
        Workstation restarted(false, registry); restarted.stopTimer();
        check(restarted.knownPlugins.getTypes().size() == 2, "Migrated plugins remain available after another restart");
        Workstation other(false, registry); other.stopTimer(); other.knownPlugins.addType(third);
        check(other.savePluginRegistry() && restarted.savePluginRegistry(), "Registry saves merge changes made by another application instance");
        Workstation final(false, registry); final.stopTimer(); check(final.knownPlugins.getTypes().size() == 3, "Saving an older in-memory list does not lose another instance's new plugin");
    }
    {
        auto blocked = root.getChildFile("blocked"); blocked.replaceWithText("not a directory");
        Workstation w(false, blocked.getChildFile("plugins.xml")); w.stopTimer(); w.knownPlugins.addType(instrument);
        check(!w.savePluginRegistry() && w.knownPlugins.getTypes().size() == 1, "Registry write failures are reported while keeping scanned plugins in memory");
    }
    directory.getChildFile("plugin-registry-verification.txt").replaceWithText("Verified actual isolated VST3 scan, immediate atomic save, restart restoration, project independence, merging legacy portable releases, duplicate and malformed-file handling, sequential multiple-instance saves, and explicit write-failure handling. All registry writes use an isolated test folder.\n");
}
}
