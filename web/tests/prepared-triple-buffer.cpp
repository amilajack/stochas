// SPDX-License-Identifier: GPL-3.0-only
#include "PreparedTripleBuffer.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>

int main() {
   PreparedTripleBufferSlots slots;
   std::array<std::array<uint64_t, 256>, 3> payload{};
   std::atomic<bool> finished{false};
   std::thread writer([&] {
      for (uint64_t revision = 1; revision <= 150000; ++revision) {
         const auto owned = slots.writerSlot();
         payload[owned].fill(revision);
         slots.publish();
         // Match SeqDataBuffer's writer-side copy for the next live edit.
         payload[slots.writerSlot()] = payload[owned];
         if (!(revision % 101)) std::this_thread::yield();
      }
      finished.store(true, std::memory_order_release);
   });
   uint64_t last = 0, reads = 0;
   do {
      slots.consume();
      const auto &snapshot = payload[slots.readerSlot()];
      const auto revision = snapshot[0];
      assert(revision >= last);
      for (const auto value : snapshot) assert(value == revision);
      last = revision; ++reads;
      if (!(reads % 71)) std::this_thread::yield();
   } while (!finished.load(std::memory_order_acquire));
   writer.join();
   slots.consume();
   assert(payload[slots.readerSlot()][0] == 150000);
   // Latest edit is always observable even if no audio callback consumed it.
   assert(payload[slots.publishedSlot()][0] == 150000);
   std::cout << "PASS triple-buffer ownership:150000 publications, " << reads << " coherent audio snapshots\n";
}
