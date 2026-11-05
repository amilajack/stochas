// SPDX-License-Identifier: GPL-3.0-only
#include "PcgAdvance.h"
#include "pcg_basic.h"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
   for (uint64_t seed : {0ULL, 1ULL, 42ULL, 0xffffffffffffffffULL}) {
      for (uint64_t increment : {1ULL, 999999587ULL, 2000037797ULL, 0xffffffffffffffffULL}) {
         for (uint64_t distance : {0ULL, 1ULL, 2ULL, 999ULL, 1000ULL, 6006000ULL}) {
            pcg32_random_t reference{seed, increment};
            for (uint64_t i = 0; i < distance; ++i) pcg32_random_r(&reference);
            assert(stochasPcgAdvance(seed, increment, distance) == reference.state);
         }
      }
   }
   const uint64_t a = 0x100000000ULL, b = 0x1000000ULL;
   assert(stochasPcgAdvance(stochasPcgAdvance(42, 17, a), 17, b) == stochasPcgAdvance(42, 17, a + b));
   std::cout << "PASS PCG exact jump-ahead vs original generator, zero/overflow/long seeks\n";
}
