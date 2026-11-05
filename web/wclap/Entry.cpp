// SPDX-License-Identifier: GPL-3.0-only
#include "Plugin.h"
#include "version.h"
#include <cstring>
namespace Stochas::Wclap {
std::string bundlePath = "/";
namespace {
const char *const features[]{"note-effect", "sequencer", "stochastic", nullptr};
uint32_t count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t *description(const clap_plugin_factory_t*, uint32_t index) {
   return index == 0 ? &descriptor : nullptr;
}
const clap_plugin_t *create(const clap_plugin_factory_t*, const clap_host_t *host, const char *id) {
   if (!host || !id || std::strcmp(id, descriptor.id) || !clap_version_is_compatible(host->clap_version)) return nullptr;
   return createPlugin(host);
}
const clap_plugin_factory_t factory{count, description, create};
bool initialise(const char *path) {
   bundlePath = path && *path ? path : "/";
   while (bundlePath.size() > 1 && bundlePath.back() == '/') bundlePath.pop_back();
   return true;
}
void deinitialise() {}
const void *getFactory(const char *id) {
   return id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr;
}
}
const clap_plugin_descriptor_t descriptor{
   CLAP_VERSION_INIT, "org.surge-synth-team.stochas", "Stochas", "Surge Synth Team",
   "https://stochas.org/", "https://stochas.org/documentation/",
   "https://github.com/amilajack/stochas", Build::FullVersionStr,
   "Randomization-enabled step sequencer", features,
};
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
   CLAP_VERSION_INIT, Stochas::Wclap::initialise,
   Stochas::Wclap::deinitialise, Stochas::Wclap::getFactory,
};
