// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#if JUCE_TARGET_HAS_BINARY_DATA
#include "BinaryData.h"
#endif
using namespace juce;
namespace ProjectInfo {
inline constexpr const char* projectName = "Stochas";
inline constexpr const char* companyName = "Surge Synth Team";
inline constexpr const char* versionString = "1.3.10";
inline constexpr int versionNumber = 0x1030a;
}
