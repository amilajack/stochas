// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>

// One control writer, one audio consumer. Each owns one slot; the third is
// exchanged atomically. A publication never writes a slot owned by audio.
// Payload construction/copying belongs to the control writer, not consume().
class PreparedTripleBufferSlots {
   static constexpr unsigned Dirty = 4, Index = 3;
   static_assert(std::atomic<unsigned>::is_always_lock_free,
                 "prepared publication requires lock-free atomics");
   std::atomic<unsigned> middle{1};
   std::atomic<unsigned> front{0};
   unsigned back = 2, published = 1;
public:
   unsigned writerSlot() const noexcept { return back; }
   unsigned publishedSlot() const noexcept { return published; }
   unsigned readerSlot() const noexcept { return front.load(std::memory_order_acquire); }
   void publish() noexcept {
      published = back;
      back = middle.exchange(back | Dirty, std::memory_order_acq_rel) & Index;
   }
   bool consume() noexcept {
      if (!(middle.load(std::memory_order_acquire) & Dirty)) return false;
      const unsigned previous = front.load(std::memory_order_relaxed);
      front.store(middle.exchange(previous, std::memory_order_acq_rel) & Index,
                  std::memory_order_release);
      return true;
   }
};
