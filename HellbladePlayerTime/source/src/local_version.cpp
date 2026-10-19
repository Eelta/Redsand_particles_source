#include <SKSE/SKSE.h>
using namespace std::literals;
extern "C" __declspec(dllexport) constinit auto SKSEPlugin_Version=[]() noexcept {
    SKSE::PluginVersionData data;
    data.PluginVersion(REL::Version{1,3,5,5});
    data.PluginName("HellbladeTimedBlock"sv);
    data.AuthorName("thisdarkocean; local player-time patch"sv);
    data.UsesAddressLibrary();data.UsesUpdatedStructs();return data;
}();
extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*,SKSE::PluginInfo* info) {
    info->infoVersion=SKSE::PluginInfo::kVersion;info->name=SKSEPlugin_Version.pluginName;info->version=SKSEPlugin_Version.pluginVersion;return true;
}
