// SPDX-License-Identifier: GPL-3.0-only
#include "Protocol.h"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
using namespace Stochas::Wclap::Protocol;
int main() {
   for (size_t length = 0; length < headerSize; ++length) {
      std::array<uint8_t, 4> raw{};
      Reader reader(raw.data(), length);
      uint64_t value = 123; const uint8_t *data = nullptr;
      assert(!reader.valid() && !reader.get(value) && !reader.getBytes(1, data));
      assert(value == 123 && reader.type() == Type(0) && reader.remaining() == 0);
   }
   Reader missing(nullptr, 4); assert(!missing.valid());
   std::array<uint8_t, 4> raw{Hello, 0, 0, 0};
   Reader huge(raw.data(), maxFrameBytes + 1); assert(!huge.valid());
   for (int byte = 1; byte < 4; ++byte) {
      raw[byte] = 1; Reader reserved(raw.data(), raw.size()); assert(!reserved.valid()); raw[byte] = 0;
   }
   Writer writer(Param);
   writer.put(uint32_t(1569)).put(double(0.125));
   Reader valid(writer.bytes.data(), writer.bytes.size());
   uint32_t identifier = 0; double value = 0;
   assert(valid.valid() && valid.type() == Param && valid.get(identifier) && valid.get(value));
   assert(identifier == 1569 && value == .125 && valid.remaining() == 0);
   const uint8_t *data = nullptr;
   assert(!valid.getBytes(std::numeric_limits<size_t>::max(), data));
   assert(!valid.get(identifier));
   // Every truncated primitive rejects without advancing or reading beyond it.
   for (size_t size = headerSize; size < writer.bytes.size(); ++size) {
      Reader truncated(writer.bytes.data(), size);
      uint64_t a = 0, b = 0;
      if (truncated.get(a)) assert(!truncated.get(b));
   }
   std::cout << "PASS webview framing: truncation/null/oversize/reserved/overflow/primitive roundtrip\n";
}
