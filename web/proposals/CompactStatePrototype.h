// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Assessment only: not an accepted/shipping state format. Every original model
// field is encoded explicitly; no native object layout crosses a module ABI.
#include "SequenceData.h"
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
class StochasCompactStatePrototype {
   static void u8(std::vector<uint8_t> &out,uint8_t value){out.push_back(value);}
   static void integer(std::vector<uint8_t> &out,uint64_t value,unsigned bytes) {
      for(unsigned i=0;i<bytes;++i)out.push_back(uint8_t(value>>(i*8)));
   }
   static void text(std::vector<uint8_t> &out,const char *value) {
      const auto size=std::strlen(value);
      if(size>255)throw std::runtime_error("prototype string exceeds schema");
      u8(out,(uint8_t)size);out.insert(out.end(),value,value+size);
   }
public:
   static constexpr size_t MaximumBytes=2*1024*1024;
   // Include every row with 64 distinct runs, every legal chain slot, every
   // mapping, and maximum-length strings. Sparsity is not needed for the cap.
   static constexpr size_t MaximumPatternBytes = SEQ_PATTERN_NAME_MAXLEN + 2 +
      SEQ_MAX_ROWS * (2 + 5 * SEQ_MAX_STEPS) + 1 +
      SEQ_MAX_STEPS * (2 + 4 * SEQ_MAX_CHAIN_SOURCES);
   static constexpr size_t MaximumLayerBytes = 16 * 4 + SEQ_LAYER_NAME_MAXLEN +
      SEQ_KEY_NAME_MAXLEN + SEQ_SCALE_NAME_MAXLEN + 1 +
      SEQ_MAX_ROWS * (3 + SEQ_MAX_NOTELABEL_LEN) + SEQ_MAX_PATTERNS * MaximumPatternBytes;
   static constexpr size_t ProvenMaximumBytes = 14 + 8 + 8 + 5 * 4 +
      SEQ_DEFAULT_NUM_STEPS + 2 + SEQMIDI_MAX_ITEMS * 6 + SEQ_MAX_LAYERS * MaximumLayerBytes;
   static_assert(ProvenMaximumBytes <= MaximumBytes, "all legal model fields must fit the prepared output");
   static size_t row(const SequenceData &state,int l,int p,int r,std::array<uint8_t,321> &out) {
      const auto &cells=state.mLayers[l].mPats[p].mRows[r].mSteps;
      unsigned runs=0;size_t at=1;bool meaningful=false;
      for(unsigned c=0;c<SEQ_MAX_STEPS;) {
         const auto &cell=cells[c];unsigned end=c+1;
         meaningful=meaningful||cell.prob!=SEQ_PROB_OFF||cell.velo||cell.length||cell.offset;
         while(end<SEQ_MAX_STEPS&&cells[end].prob==cell.prob&&cells[end].velo==cell.velo&&
               cells[end].length==cell.length&&cells[end].offset==cell.offset)++end;
         out[at++]=(uint8_t)(end-c);out[at++]=(uint8_t)cell.prob;out[at++]=(uint8_t)cell.velo;
         out[at++]=(uint8_t)cell.length;out[at++]=(uint8_t)cell.offset;++runs;c=end;
      }
      out[0]=(uint8_t)runs;return meaningful?at:0;
   }
   static void encode(const SequenceData &state,std::vector<uint8_t> &out) {
      if(out.capacity()<MaximumBytes)out.reserve(MaximumBytes);
      out.clear();out.insert(out.end(),{'S','T','C','P'});
      integer(out,1,2);integer(out,0,2);u8(out,SEQ_MAX_LAYERS);u8(out,SEQ_MAX_PATTERNS);
      integer(out,SEQ_MAX_ROWS,2);integer(out,SEQ_MAX_STEPS,2);
      integer(out,(uint64_t)state.mRandomSeed,8);
      static_assert(sizeof(double)==8&&std::numeric_limits<double>::is_iec559);
      uint64_t bits;std::memcpy(&bits,&state.mStandaloneBPM,8);integer(out,bits,8);
      for(int value:{state.mSwing,state.mMidiPassthru,state.mMidiRespond,state.mOffsetTime,state.mAutoPlay})integer(out,(uint32_t)value,4);
      for(int value:state.mGroove)u8(out,(uint8_t)value);
      integer(out,state.mMidiMapCount,2);
      for(int i=0;i<state.mMidiMapCount;++i) {
         const auto &m=state.mMidiMap[i];for(int8_t value:{m.mAction,m.mTarget,m.mValue,m.mType,m.mNote,m.mChannel})u8(out,(uint8_t)value);
      }
      std::array<uint8_t,321> encodedRow{};
      for(int l=0;l<SEQ_MAX_LAYERS;++l) {
         const auto &layer=state.mLayers[l];
         const int fields[]{layer.mNumRows,layer.mNumSteps,layer.mIsMonoMode,layer.mMaxPoly,layer.mPolyBias,layer.mCurrentNoteSet,
            layer.mCurrentPattern,layer.mClockDivider,layer.mMidiChannel,layer.mDutyCycle,layer.mStepsPerMeasure,
            layer.mMuted,layer.mHumanLen,layer.mHumanVelo,layer.mHumanPos,layer.mCombineMode};
         for(int value:fields)integer(out,(uint32_t)value,4);
         text(out,layer.mLayerName);text(out,layer.mStdKeyName);text(out,layer.mStdScaleName);u8(out,(uint8_t)layer.mStdOctave);
         for(int r=0;r<SEQ_MAX_ROWS;++r) {
            u8(out,(uint8_t)layer.mNoteSets[SequenceLayer::standardSet].notes[r].note);
            const auto &custom=layer.mNoteSets[SequenceLayer::customSet].notes[r];u8(out,(uint8_t)custom.note);text(out,custom.noteName);
         }
         for(int p=0;p<SEQ_MAX_PATTERNS;++p) {
            const auto &pattern=layer.mPats[p];text(out,pattern.mName);
            const auto countAt=out.size();integer(out,0,2);unsigned count=0;
            for(int r=0;r<SEQ_MAX_ROWS;++r) {
               const auto size=row(state,l,p,r,encodedRow);
               if(size){++count;u8(out,(uint8_t)r);out.insert(out.end(),encodedRow.begin(),encodedRow.begin()+size);}
            }
            out[countAt]=(uint8_t)count;out[countAt+1]=(uint8_t)(count>>8);
            const auto columnsAt=out.size();u8(out,0);unsigned columns=0;
            for(int c=0;c<SEQ_MAX_STEPS;++c) {
               unsigned count=0;for(const auto &link:pattern.mChains[c].cells)if(link.flags&SEQ_CHAIN_FLAG_USED)++count;
               if(!count)continue;
               ++columns;u8(out,(uint8_t)c);u8(out,(uint8_t)count);
               for(const auto &link:pattern.mChains[c].cells)if(link.flags&SEQ_CHAIN_FLAG_USED)
                  for(int value:{(int)link.col,(int)link.row,(int)link.targetrow,(int)link.flags})u8(out,(uint8_t)value);
            }
            out[columnsAt]=(uint8_t)columns;
         }
      }
      if(out.size()>MaximumBytes)throw std::runtime_error("prototype exceeded proven fixed model bound");
   }
};
