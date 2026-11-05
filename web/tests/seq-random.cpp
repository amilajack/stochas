// SPDX-License-Identifier: GPL-3.0-only
// Original random-access control follows Andrew Shakinovsky's SeqRandom at
// adf7f55b679ec6d542f6ce5441e01bab097494ae, including rejection-draw caching.
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
// SeqRandom only needs these two JUCE aliases/assertions from Constants.h.
// Compile the actual production implementation without loading the GUI stack.
#define CONSTANTS_H_
using uint64 = uint64_t;
#define jassert(value) assert(value)
#include "SeqRandom.cpp"
class Original {
   std::array<uint64_t, NUM_KEY_FRAMES + 1> frames{};
   uint64_t sequence = 0, last = 0;
   int highest = 0, lastFrame = -1, lastPosition = -1;
public:
   void seed(uint64_t seed, uint64_t seq) {
      pcg32_random_t root;
      pcg32_srandom_r(&root, seed, seq);
      pcg32_random_r(&root);
      frames[0] = root.state;
      sequence = seq; highest = 0; lastFrame = lastPosition = -1;
   }
   int get(int pos, int size) {
      const int position = pos % (NUM_KEY_FRAMES * FRAME_SIZE);
      const int frame = position / FRAME_SIZE, offset = position % FRAME_SIZE;
      if (highest < frame) {
         pcg32_random_t start{frames[highest], sequence};
         for (int i = highest + 1; i <= frame; ++i) {
            for (int j = 0; j < FRAME_SIZE; ++j) pcg32_random_r(&start);
            frames[i] = start.state;
         }
         highest = frame;
      }
      pcg32_random_t current{frames[frame], sequence};
      int count = offset;
      if (lastFrame == frame && lastPosition < offset) {
         count = offset - lastPosition - 1;
         current.state = last;
      }
      while (count--) pcg32_random_r(&current);
      const int result = pcg32_boundedrand_r(&current, size);
      if (offset == FRAME_SIZE - 1 && frame == highest) frames[++highest] = current.state;
      lastFrame = frame; lastPosition = offset; last = current.state;
      return result;
   }
};
int main() {
   std::mt19937 random(0x5331);
   constexpr int span = NUM_KEY_FRAMES * FRAME_SIZE;
   uint64_t queries = 0;
   for (uint64_t seed : {0ULL, 42ULL, 0xffffffffffffffffULL}) {
      for (uint64_t sequence : {1ULL, 999999587ULL, 0xffffffffffffffffULL}) {
         auto candidate = std::make_unique<SeqRandom>();
         auto original = std::make_unique<Original>();
         candidate->setRandomSeedAndSequence(seed, sequence);
         original->seed(seed, sequence);
         auto compare = [&](int position, int bound) {
            assert(candidate->getRandAtPosition(position, bound) == original->get(position, bound));
            ++queries;
         };
         // Sequential boundary caches, repeated positions and wrapped final frame.
         for (int position = 0; position < 3001; ++position) compare(position, 100);
         for (int position : {span-2,span-1,span,span+1,999,1000,1999,2000,0,0})
            compare(position, 2147483647);
         for (int i = 0; i < 12000; ++i) {
            const int bounds[]{1,2,3,7,100,128,2147483647};
            compare(int(random() % (span * 3)), bounds[random() % 7]);
         }
      }
   }
   std::cout << "PASS actual SeqRandom vs original seek/sequential/rejection/wrap control: " << queries << " queries\n";
}
