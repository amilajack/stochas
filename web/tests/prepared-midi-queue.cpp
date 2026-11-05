// SPDX-License-Identifier: GPL-3.0-only
#include "PreparedMidiQueue.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <tuple>
#include <vector>

int main() {
   using Queue = PreparedMidiQueue<4096>;
   Queue queue;
   std::array<int, 4096> slots{};
   for (int i = 0; i < 4096; ++i) {
      slots[i] = queue.insert({4096 - i, i % 128, i % 2 ? 100 : 0, i % 16 + 1, -1});
      assert(slots[i] == i);
   }
   assert(queue.available() == 0);
   assert(queue.insert({0, 60, 100, 1, -1}) == -1);
   assert(queue.size() == 4096); // Refusal leaves every admitted event intact.
   for (int i = 4095; i >= 0; --i) {
      assert(queue.first() == i);
      queue.erase(i);
   }
   assert(queue.size() == 0 && queue.available() == 4096);
   // Off lookup isolates pitch+channel and preserves original lowest-slot order.
   const int a = queue.insert({100, 127, 0, 16, -1});
   const int b = queue.insert({200, 127, 0, 16, -1});
   assert(queue.matchingOff(127, 16, 0) == a);
   assert(queue.matchingOff(127, 1, 0) == -1);
   queue.setTime(a, 300);
   assert(queue.first() == b && queue.matchingOff(127, 16, 150) == a);
   queue.erase(a);
   assert(queue.matchingOff(127, 16, 150) == b);
   queue.clear();
   assert(queue.matchingOff(127, 16, 0) == -1);
   // Stable off-before-on tie ordering, including arbitrary time updates.
   int on = queue.insert({1, 60, 100, 1, -1});
   int off = queue.insert({1, 60, 0, 1, -1});
   assert(queue.first() == off);
   queue.setTime(off, 2); assert(queue.first() == on);
   queue.setTime(off, 0); assert(queue.first() == off);
   queue.erase(off); queue.erase(off); // Duplicate removal is harmless.
   queue.erase(on);
   assert(queue.insert({1, -1, 100, 1, -1}) == -1);
   assert(queue.insert({1, 128, 100, 1, -1}) == -1);
   assert(queue.insert({1, 60, 100, 17, -1}) == -1);
   assert(queue.insert({1, 60, 128, 1, -1}) == -1);
   on = queue.insert({10, 60, 100, 1, -1});
   off = queue.insert({20, 60, 0, 1, -1});
   queue.setCorrespondingOff(on, off);
   queue.erase(on);
   assert(queue.get(off).correspondingOn == -1);
   const int reused = queue.insert({15, 61, 100, 1, -1});
   assert(reused == on && queue.get(off).correspondingOn == -1);
   queue.erase(off); queue.erase(reused);
   // Independent sorted reference across insert/change/erase, not a mirror of
   // heap implementation. Host/reference allocations live outside this queue.
   std::mt19937 random(0x5a17);
   for (int iteration = 0; iteration < 50000; ++iteration) {
      const int slot = random() % 4096;
      if (queue.contains(slot)) {
         if (random() % 2) queue.erase(slot);
         else queue.setTime(slot, random() % 10000);
      } else if (queue.available()) {
         queue.insert({random() % 10000, int(random() % 128), int(random() % 128), int(random() % 16 + 1), -1});
      }
      if (iteration % 37 == 0) {
         std::vector<int> reference;
         for (int i = 0; i < 4096; ++i) if (queue.contains(i)) reference.push_back(i);
         std::sort(reference.begin(), reference.end(), [&](int left, int right) {
            const auto &a = queue.get(left), &b = queue.get(right);
            return std::make_tuple(a.sample, a.velocity != 0, left) < std::make_tuple(b.sample, b.velocity != 0, right);
         });
         assert(int(reference.size()) == queue.size());
         assert(reference.empty() ? queue.first() == -1 : queue.first() == reference.front());
         for (int note : {0, 60, 127}) for (int channel : {1, 16}) {
            int match = -1;
            for (int i = 0; i < 4096; ++i) if (queue.contains(i)) {
               const auto &e = queue.get(i);
               if (e.note == note && e.channel == channel && e.velocity == 0 && e.sample > 100 && match == -1) match = i;
            }
            assert(queue.matchingOff(note, channel, 100) == match);
         }
      }
   }
   queue.forEachSlot([&](int slot, const auto&) { queue.erase(slot); });
   assert(queue.size() == 0 && queue.first() == -1);
   std::cout << "PASS prepared MIDI queue: capacity/refusal, stable ordering, note127, updates/cancellation,50000 randomized operations\n";
}
