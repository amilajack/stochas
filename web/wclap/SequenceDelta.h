// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "SequenceData.h"
#include "Protocol.h"
#include <memory>
#include <vector>

// Explicit semantic wire fields. No SequenceData/Cell/Chain native object
// layouts cross the module boundary. This control-thread codec skips unchanged
// pattern/row blocks; a single drag encodes only its changed cells/controls.
class SeqWireDelta {
public:
   struct Encoded {
      std::vector<uint8_t> bytes;
      bool needsSnapshot = false;
   };
   static Encoded encode(const SequenceData &before, const SequenceData &after,
                         uint64_t expectedRevision);
   static std::unique_ptr<SequenceData> prepare(const SequenceData &current,
      const void *frame, size_t size, uint64_t expectedRevision, uint64_t &nextRevision);
   // Fast plugin-main path: staging already mirrors current and belongs only
   // to the control writer. Success allocates/copies no full model. Refusal
   // restores staging from current before returning; current is never mutated.
   static bool applyPrepared(SequenceData &staging,const SequenceData &current,
      const void *frame,size_t size,uint64_t expectedRevision,uint64_t &nextRevision);
   // Editor-only three-way rebase: retain remote recording changes while
   // replaying fields edited locally since the last acknowledged model.
   static void rebase(const SequenceData &base,const SequenceData &local,
      const SequenceData &remote,SequenceData &result);
private:
   static bool decode(SequenceData &staging,const void *frame,size_t size,
      uint64_t expectedRevision,uint64_t &nextRevision);
};
