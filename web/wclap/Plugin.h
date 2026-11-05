// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <clap/clap.h>
#include <string>
namespace Stochas::Wclap {
extern const clap_plugin_descriptor_t descriptor;
extern std::string bundlePath;
const clap_plugin_t *createPlugin(const clap_host_t *host);
}
