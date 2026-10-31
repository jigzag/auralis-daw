#pragma once
#include "Engine.h"
namespace auralis {
struct PluginUse { String kind, purpose; };
PluginUse describePlugin(const PluginDescription&);
String pluginMenuLabel(const PluginDescription&);
String pluginHelp(const PluginDescription&);
PopupMenu pluginPurposeMenu(const Array<PluginDescription>&, int firstId);
}
