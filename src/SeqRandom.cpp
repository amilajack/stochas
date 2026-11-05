/***************************************************************
 ** Copyright (C) 2016 by Andrew Shakinovsky
 **
 ** You may also use this code under the terms of the 
 ** GPL v3 (see www.gnu.org/licenses).
 ** STOCHAS IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL 
 ** WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING 
 ** MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE DISCLAIMED.
 ***************************************************************/

#include "SeqRandom.h"
#include "PcgAdvance.h"

// set the seed used for this sequence of random numbers.
// sequence number allows us to get a different sequence of rng values
// when using the same seed from another instance

void SeqRandom::setRandomSeedAndSequence(uint64 seed, uint64 seqno)
{
   pcg32_random_t root;
   mHighestCalculatedFrame = 0;
   for (auto &valid : mKeyFrameValid) valid = false;
   mKeyFrameValid[0] = true;
   // these two help avoid over-generating
   mLastGeneratedPosition = -1;
   mLastGeneratedFrame = -1;

   jassert(seqno % 2 != 0); // must be odd according to pcg
   mSequenceNumber = seqno;

   // get frame 0 (root)
   pcg32_srandom_r(&root, seed, seqno);
   pcg32_random_r(&root); // generate one random number for the heck of things
   mKeyFrames[0] = root.state;

   mStats = Statistics();
}

// main function to get a random number at a position.
// this will wrap around when position exceeds FRAME_SIZE*NUM_KEY_FRAMES

int SeqRandom::getRandAtPosition(int pos, int size)
{
   pcg32_random_t cur;
   cur.inc = mSequenceNumber;

   // position in our frame set
   // we start wrapping after this
   int position = pos % (NUM_KEY_FRAMES*FRAME_SIZE);

   // determine which frame we are in
   int frame = position / FRAME_SIZE;

   // which position in that frame
   int pos_in_frame = position % FRAME_SIZE;

   int ctr;

   // A seek may cross six million discarded random values. Fill only the
   // requested frame by exact LCG jump-ahead, deriving gaps lazily from the
   // nearest already-known lower frame. Preserve the original highest-frame
   // rule and cached boundary states, including bounded-random rejection draws.
   if (!mKeyFrameValid[frame]) {
      int previous = frame - 1;
      while (previous > 0 && !mKeyFrameValid[previous]) --previous;
      const uint64_t distance = uint64_t(frame - previous) * FRAME_SIZE;
      mKeyFrames[frame] = stochasPcgAdvance(mKeyFrames[previous], mSequenceNumber, distance);
      mKeyFrameValid[frame] = true;
      mStats.generations += (int)distance;
      if (frame > mHighestCalculatedFrame) mHighestCalculatedFrame = frame;
   }
   else
      mStats.frame_saves++;

   // save time if moving sequentially
   // if frame is same as last time and we are in a greater position in that
   // frame, just catch up
   if (mLastGeneratedFrame == frame && mLastGeneratedPosition < pos_in_frame) {
      ctr = (pos_in_frame - mLastGeneratedPosition) - 1;
      cur.state = mLastGenerated;
      if (ctr)
         mStats.seq_saves++;
   }
   else {
      ctr = pos_in_frame;
      cur.state = mKeyFrames[frame];
   }

   // generate intermediate values
   mStats.generations += ctr;
   cur.state = stochasPcgAdvance(cur.state, cur.inc, (uint64_t)ctr);

   // save time next time if we are moving sequentially      
   mLastGeneratedPosition = pos_in_frame;
   mLastGeneratedFrame = frame;

   int ret = pcg32_boundedrand_r(&cur, size);
   // if we are on the last position of a previous frame
   // save generations as well because this will be the value of the next keyframe
   if (pos_in_frame == FRAME_SIZE - 1 && frame == mHighestCalculatedFrame && frame + 1 < NUM_KEY_FRAMES) {
      mHighestCalculatedFrame++;
      mKeyFrames[mHighestCalculatedFrame] = cur.state;
      mKeyFrameValid[mHighestCalculatedFrame] = true;
      mStats.end_saves++;
   }
   mLastGenerated = cur.state;
   mStats.actual_numbers++;
   return ret;
}

void SeqRandom::prepareSeqPosition(int pos)
{
   mSeqPosition = pos;
   mSeqPositionSub = 0;
}

int SeqRandom::getNextRandom(int size)
{
   int pos = (mSeqPosition*MAX_PER_SEQ_POSITION) + (mSeqPositionSub++);

   // if we start going over this number, it will need to be increased
   jassert(mSeqPositionSub < MAX_PER_SEQ_POSITION);

   return getRandAtPosition(pos,size);
}
