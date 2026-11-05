// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PluginProcessor.h"
#include <clap/clap.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace Stochas::Wclap {
// The original legacy JUCE parameters use decimal index strings. The pinned
// native clap-juce wrapper hashes those with JUCE's31-based String hash. These
// are the actual native IDs ("0"→48,"10"→1567), not assumed index-as-ID values.
class Parameters {
   static constexpr size_t Capacity = 55;
   std::array<clap_id, Capacity> identifiers{};
   SeqAudioProcessor &processor;
public:
   explicit Parameters(SeqAudioProcessor &p) : processor(p) {
      jassert(p.getParameters().size() == Capacity);
      for (size_t index = 0; index < Capacity; ++index) {
         char number[16]{}; std::snprintf(number, sizeof(number), "%u", (unsigned)index);
         clap_id id = 0;
         for (const char *s = number; *s; ++s) id = 31 * id + (unsigned char)*s;
         identifiers[index] = id;
      }
   }
   uint32_t count() const noexcept { return (uint32_t)processor.getParameters().size(); }
   clap_id idAt(unsigned index) const noexcept { return index<Capacity?identifiers[index]:CLAP_INVALID_ID; }
   juce::AudioProcessorParameter *find(clap_id id) const noexcept {
      for (size_t index = 0; index < Capacity; ++index)
         if (identifiers[index] == id) return processor.getParameters()[(int)index];
      return nullptr;
   }
   bool info(uint32_t index, clap_param_info_t *output) const {
      if (!output || index >= count() || index >= Capacity) return false;
      *output = {};
      auto *parameter = processor.getParameters()[(int)index];
      output->id = identifiers[index]; output->cookie = parameter;
      output->flags = parameter->isAutomatable() ? CLAP_PARAM_IS_AUTOMATABLE : 0;
      output->min_value = 0; output->max_value = 1; output->default_value = parameter->getDefaultValue();
      const auto name = parameter->getName(CLAP_NAME_SIZE - 1);
      name.copyToUTF8(output->name, CLAP_NAME_SIZE);
      return true;
   }
   bool set(clap_id id, double value) noexcept {
      auto *parameter = find(id);
      if (!parameter || !std::isfinite(value) || value < 0 || value > 1) return false;
      parameter->setValue((float)value); return true;
   }
};
}
