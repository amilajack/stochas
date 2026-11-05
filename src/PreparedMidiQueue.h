/***************************************************************
 ** Copyright (C) 2026 Stochas contributors
 ** GPL v3. See COPYING. No allocation or locking on the processing thread.
 ***************************************************************/
#pragma once
#include <array>
#include <cstdint>
#include <limits>

// Prepared indexed min-heap. Event slots stay stable while their deadlines
// change, and same-note cancellation never scans unrelated notes. No storage
// grows in response to MIDI density; callers must report/refuse overflow.
template <int Capacity> class PreparedMidiQueue {
public:
   static_assert(Capacity > 0 && Capacity <= 65535);
   struct Event {
      int64_t sample = 0;
      int note = 0;
      int velocity = 0;
      int channel = 0;
      int correspondingOff = -1;
      int correspondingOn = -1;
   };
private:
   static constexpr int WordCount = (Capacity + 63) / 64;
   std::array<Event, Capacity> events{};
   std::array<int, Capacity> heap{};
   std::array<int, Capacity> positions{};
   std::array<int, Capacity> nextOff{};
   std::array<int, Capacity> previousOff{};
   std::array<int, 16 * 128> firstOff{};
   std::array<uint64_t, WordCount> freeWords{};
   int count = 0;

   bool before(int left, int right) const noexcept {
      const auto &a = events[left]; const auto &b = events[right];
      if (a.sample != b.sample) return a.sample < b.sample;
      if ((a.velocity == 0) != (b.velocity == 0)) return a.velocity == 0;
      return left < right; // Stable native slot order for simultaneous events.
   }
   void exchange(int a, int b) noexcept {
      const int tmp = heap[a]; heap[a] = heap[b]; heap[b] = tmp;
      positions[heap[a]] = a; positions[heap[b]] = b;
   }
   void upwards(int index) noexcept {
      while (index > 0) {
         const int parent = (index - 1) / 2;
         if (!before(heap[index], heap[parent])) break;
         exchange(index, parent); index = parent;
      }
   }
   void downwards(int index) noexcept {
      for (;;) {
         const int left = index * 2 + 1;
         if (left >= count) return;
         const int right = left + 1;
         const int child = right < count && before(heap[right], heap[left]) ? right : left;
         if (!before(heap[child], heap[index])) return;
         exchange(index, child); index = child;
      }
   }
   void relinkOff(int slot) noexcept {
      const auto &event = events[slot];
      const int key = (event.channel - 1) * 128 + event.note;
      previousOff[slot] = -1; nextOff[slot] = firstOff[key];
      if (firstOff[key] != -1) previousOff[firstOff[key]] = slot;
      firstOff[key] = slot;
   }
public:
   PreparedMidiQueue() noexcept { clear(); }
   void clear() noexcept {
      count = 0; positions.fill(-1); nextOff.fill(-1); previousOff.fill(-1); firstOff.fill(-1);
      freeWords.fill(std::numeric_limits<uint64_t>::max());
      if constexpr (Capacity % 64 != 0) freeWords.back() = (uint64_t{1} << (Capacity % 64)) - 1;
   }
   int size() const noexcept { return count; }
   int available() const noexcept { return Capacity - count; }
   bool contains(int slot) const noexcept { return slot >= 0 && slot < Capacity && positions[slot] != -1; }
   const Event &get(int slot) const noexcept { return events[slot]; }
   int first() const noexcept { return count ? heap[0] : -1; }
   // The caller checks that there is enough capacity for a complete note pair
   // before insertion, so a rejected note cannot leave an orphan on/off.
   int insert(Event event) noexcept {
      if (event.note < 0 || event.note > 127 || event.channel < 1 || event.channel > 16 ||
          event.velocity < 0 || event.velocity > 127 || count == Capacity) return -1;
      int slot = -1;
      for (int word = 0; word < WordCount; ++word) {
         if (freeWords[word]) {
            const int bit = __builtin_ctzll(freeWords[word]);
            freeWords[word] &= ~(uint64_t{1} << bit); slot = word * 64 + bit; break;
         }
      }
      if (slot == -1) return -1;
      events[slot] = event;
      if (event.velocity == 0) relinkOff(slot);
      heap[count] = slot; positions[slot] = count; ++count; upwards(count - 1);
      return slot;
   }
   void setCorrespondingOff(int on, int off) noexcept {
      events[on].correspondingOff = off;
      events[off].correspondingOn = on;
   }
   void setTime(int slot, int64_t sample) noexcept {
      const int position = positions[slot];
      const auto old = events[slot].sample; events[slot].sample = sample;
      if (sample < old) upwards(position); else downwards(position);
   }
   int matchingOff(int note, int channel, int64_t after) const noexcept {
      if (note < 0 || note > 127 || channel < 1 || channel > 16) return -1;
      int result = -1;
      // Preserve the original lowest-slot match when several retriggers overlap.
      for (int slot = firstOff[(channel - 1) * 128 + note]; slot != -1; slot = nextOff[slot])
         if (events[slot].sample > after && (result == -1 || slot < result)) result = slot;
      return result;
   }
   void erase(int slot) noexcept {
      if (!contains(slot)) return;
      const auto &event = events[slot];
      if (contains(event.correspondingOn) && events[event.correspondingOn].correspondingOff == slot)
         events[event.correspondingOn].correspondingOff = -1;
      if (contains(event.correspondingOff) && events[event.correspondingOff].correspondingOn == slot)
         events[event.correspondingOff].correspondingOn = -1;
      if (event.velocity == 0) {
         const int previous = previousOff[slot], next = nextOff[slot];
         if (previous != -1) nextOff[previous] = next;
         else firstOff[(event.channel - 1) * 128 + event.note] = next;
         if (next != -1) previousOff[next] = previous;
         previousOff[slot] = nextOff[slot] = -1;
      }
      const int index = positions[slot]; --count;
      if (index != count) {
         heap[index] = heap[count]; positions[heap[index]] = index;
         if (index > 0 && before(heap[index], heap[(index - 1) / 2])) upwards(index);
         else downwards(index);
      }
      positions[slot] = -1; freeWords[slot / 64] |= uint64_t{1} << (slot % 64);
   }
   template <typename Function> void forEachSlot(Function function) noexcept {
      // Used only for bounded stop/seek/pattern changes, never per-event insertion
      // or routine clock advancement. Callback may erase the current slot.
      for (int slot = 0; slot < Capacity; ++slot) if (contains(slot)) function(slot, events[slot]);
   }
};
