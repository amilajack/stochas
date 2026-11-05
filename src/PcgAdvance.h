/***************************************************************
 ** Copyright (C) 2026 Stochas contributors
 ** GPL v3. See COPYING.
 ***************************************************************/
#pragma once
#include <cstdint>
// Advance the original PCG32 LCG without producing discarded output values.
// Unsigned modular arithmetic is intentional and matches repeated pcg32_random_r.
inline uint64_t stochasPcgAdvance(uint64_t state, uint64_t increment, uint64_t distance) noexcept
{
   uint64_t multiplier = 6364136223846793005ULL;
   uint64_t addition = increment;
   uint64_t accumulatedMultiplier = 1;
   uint64_t accumulatedAddition = 0;
   while (distance) { // At most64 turns for any representable distance.
      if (distance & 1) {
         accumulatedMultiplier *= multiplier;
         accumulatedAddition = accumulatedAddition * multiplier + addition;
      }
      addition = (multiplier + 1) * addition;
      multiplier *= multiplier;
      distance >>= 1;
   }
   return accumulatedMultiplier * state + accumulatedAddition;
}
