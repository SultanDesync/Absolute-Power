#include "Plugin.h"

#include "SFSEInterface.h"

SFSE_PLUGIN_VERSION = []() noexcept {
    SFSE::PluginVersionData data{};
    data.PluginVersion(Plugin::Version);
    data.PluginName(Plugin::Name);
    data.AuthorName(Plugin::Author);
    data.UsesSigScanning(true);
    data.UsesAddressLibrary(false);
    data.HasNoStructUse(false);
    data.IsLayoutDependent(true);
    data.CompatibleVersions({REL::Version{1, 16, 244, 0}});
    data.MinimumRequiredXSEVersion(REL::Version{0, 2, 20, 0});
    return data;
}();
