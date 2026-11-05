// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Portable runtime state for the unshipped WCLAP port. Original native preset
// files remain XML. Every field is explicit; no native object layout is stored.
#include "SequenceData.h"
#include "Persist.h"
#include "Scale.h"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
class SeqCompactState {
   static void u8(std::vector<uint8_t> &out,uint8_t value){out.push_back(value);}
   static void integer(std::vector<uint8_t> &out,uint64_t value,unsigned bytes) {
      for(unsigned i=0;i<bytes;++i)out.push_back(uint8_t(value>>(i*8)));
   }
   static void text(std::vector<uint8_t> &out,const char *value) {
      const auto size=std::strlen(value);
      if(size>255)throw std::runtime_error("compact state string exceeds schema");
      u8(out,(uint8_t)size);out.insert(out.end(),value,value+size);
   }
   class Reader {
      const uint8_t *data;
      size_t size,at=0;
   public:
      Reader(const void *bytes,size_t length):data(static_cast<const uint8_t*>(bytes)),size(length){}
      bool number(unsigned width,uint64_t &value) {
         if(!data||width>8||at>size||width>size-at)return false;
         value=0;for(unsigned i=0;i<width;++i)value|=uint64_t(data[at++])<<(8*i);
         return true;
      }
      bool fixed(unsigned width,uint64_t expected) {
         uint64_t value;return number(width,value)&&value==expected;
      }
      bool small(unsigned width,unsigned maximum,unsigned &value) {
         uint64_t raw;if(!number(width,raw)||raw>maximum)return false;value=(unsigned)raw;return true;
      }
      bool signed32(int &value,int minimum,int maximum) {
         uint64_t raw;if(!number(4,raw))return false;
         const uint32_t bits=(uint32_t)raw;int32_t signedValue;
         std::memcpy(&signedValue,&bits,4);
         if(signedValue<minimum||signedValue>maximum)return false;
         value=signedValue;return true;
      }
      bool byte(int8_t &value,int minimum=-128,int maximum=127) {
         uint64_t raw;if(!number(1,raw))return false;
         const int signedValue=raw<128?(int)raw:(int)raw-256;
         if(signedValue<minimum||signedValue>maximum)return false;
         value=(int8_t)signedValue;return true;
      }
      template<size_t N> bool string(char (&value)[N]) {
         uint64_t count;if(!number(1,count)||count>=N||at>size||count>size-at)return false;
         const auto *text=data+at;
         if(std::memchr(text,0,(size_t)count)||
            !juce::CharPointer_UTF8::isValidString(reinterpret_cast<const char*>(text),(int)count))return false;
         std::memset(value,0,N);std::memcpy(value,text,(size_t)count);at+=(size_t)count;return true;
      }
      bool end()const{return at==size;}
   };
public:
   static constexpr size_t MaximumBytes=2*1024*1024;
   // Include every row with 64 distinct runs, every legal chain slot, every
   // mapping, and maximum-length strings. Sparsity is not needed for the cap.
   static constexpr size_t MaximumPatternBytes = SEQ_PATTERN_NAME_MAXLEN + 2 +
      SEQ_MAX_ROWS * (2 + 5 * SEQ_MAX_STEPS) + 1 +
      SEQ_MAX_STEPS * (2 + 4 * SEQ_MAX_CHAIN_SOURCES);
   static constexpr size_t MaximumLayerBytes = 16 * 4 + SEQ_LAYER_NAME_MAXLEN +
      SEQ_KEY_NAME_MAXLEN + SEQ_SCALE_NAME_MAXLEN + 1 +
      SEQ_MAX_ROWS * (4 + 2 * SEQ_MAX_NOTELABEL_LEN) + SEQ_MAX_PATTERNS * MaximumPatternBytes;
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
      if(out.capacity()<MaximumBytes)throw std::runtime_error("compact state output was not prepared");
      out.clear();out.insert(out.end(),{'S','T','C','S'});
      integer(out,1,2);integer(out,0,2);u8(out,SEQ_MAX_LAYERS);u8(out,SEQ_MAX_PATTERNS);
      integer(out,SEQ_MAX_ROWS,2);integer(out,SEQ_MAX_STEPS,2);
      integer(out,(uint64_t)state.mRandomSeed,8);
      static_assert(sizeof(double)==8&&std::numeric_limits<double>::is_iec559);
      uint64_t bits;std::memcpy(&bits,&state.mStandaloneBPM,8);integer(out,bits,8);
      for(int value:{state.mSwing,state.mMidiPassthru,state.mMidiRespond,state.mOffsetTime,state.mAutoPlay})integer(out,(uint32_t)value,4);
      for(int value:state.mGroove)u8(out,(uint8_t)value);
      integer(out,state.mMidiMapCount,2);
      for(int i=0;i<SEQMIDI_MAX_ITEMS;++i) {
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
            const auto &standard=layer.mNoteSets[SequenceLayer::standardSet].notes[r];
            u8(out,(uint8_t)standard.note);text(out,standard.noteName);
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
               std::array<SequenceLayer::SourceCell,SEQ_MAX_CHAIN_SOURCES> links;
               unsigned count=0;
               for(const auto &link:pattern.mChains[c].cells)
                  if(link.flags&SEQ_CHAIN_FLAG_USED)links[count++]=link;
               if(!count)continue;
               std::sort(links.begin(),links.begin()+count,[](const auto &a,const auto &b) {
                  return std::tie(a.targetrow,a.row,a.col,a.flags)<std::tie(b.targetrow,b.row,b.col,b.flags);
               });
               ++columns;u8(out,(uint8_t)c);u8(out,(uint8_t)count);
               for(unsigned i=0;i<count;++i) {
                  const auto &link=links[i];
                  for(int value:{(int)link.col,(int)link.row,(int)link.targetrow,(int)link.flags})u8(out,(uint8_t)value);
               }
            }
            out[columnsAt]=(uint8_t)columns;
         }
      }
      if(out.size()>MaximumBytes)throw std::runtime_error("compact state exceeded proven fixed model bound");
   }
   // Only a caller-owned prepared candidate is mutated. Publish it after true;
   // any refusal leaves the live model untouched. Defaults are prepared once,
   // avoiding a giant stack temporary or allocations on the DSP owner.
   static bool decode(SequenceData &candidate,const SequenceData &defaults,const void *bytes,size_t size) {
      if(&candidate==&defaults||!bytes||size>ProvenMaximumBytes)return false;
      Reader in(bytes,size);
      if(!in.fixed(4,0x53435453)||!in.fixed(2,1)||!in.fixed(2,0)||
         !in.fixed(1,SEQ_MAX_LAYERS)||!in.fixed(1,SEQ_MAX_PATTERNS)||
         !in.fixed(2,SEQ_MAX_ROWS)||!in.fixed(2,SEQ_MAX_STEPS))return false;
      candidate=defaults;
      uint64_t bits;
      if(!in.number(8,bits))return false;std::memcpy(&candidate.mRandomSeed,&bits,8);
      if(!in.number(8,bits))return false;std::memcpy(&candidate.mStandaloneBPM,&bits,8);
      if(!std::isfinite(candidate.mStandaloneBPM)||candidate.mStandaloneBPM<1||candidate.mStandaloneBPM>300)return false;
      int *globals[]{&candidate.mSwing,&candidate.mMidiPassthru,&candidate.mMidiRespond,&candidate.mOffsetTime,&candidate.mAutoPlay};
      const int globalBounds[][2]{{-50,50},{SEQ_MIDI_PASSTHRU_NONE,SEQ_MIDI_PASSTHRU_ALL},{0,1},
         {SEQ_POS_OFFSET_MIN,SEQ_POS_OFFSET_MAX},{SEQ_PLAYMODE_AUTO,SEQ_PLAYMODE_MEASURE}};
      for(unsigned i=0;i<5;++i)if(!in.signed32(*globals[i],globalBounds[i][0],globalBounds[i][1]))return false;
      for(auto &value:candidate.mGroove) {int8_t groove;if(!in.byte(groove,-50,50))return false;value=groove;}
      unsigned mappings;if(!in.small(2,SEQMIDI_MAX_ITEMS,mappings))return false;candidate.mMidiMapCount=(int)mappings;
      for(auto &mapping:candidate.mMidiMap) {
         for(auto *field:{&mapping.mAction,&mapping.mTarget,&mapping.mValue,&mapping.mType,&mapping.mNote,&mapping.mChannel})
            if(!in.byte(*field))return false;
         if(!SeqPersist::validMapping(mapping))return false;
      }
      const int layerBounds[][2]{{SEQ_MIN_ROWS,SEQ_MAX_ROWS},{SEQ_MIN_STEPS,SEQ_MAX_STEPS},{0,1},{1,SEQ_MAX_ROWS},
         {SEQ_POLY_BIAS_MIN,SEQ_POLY_BIAS_MAX},{0,1},{0,SEQ_MAX_PATTERNS-1},{SEQ_MIN_CLOCK_DIV,SEQ_MAX_CLOCK_DIV},
         {1,16},{SEQ_DUTY_MIN,SEQ_DUTY_MAX},{SEQ_MIN_STEPS,SEQ_DEFAULT_NUM_STEPS},{0,1},
         {0,SEQ_MAX_HUMAN_LENGTH},{0,SEQ_MAX_HUMAN_VELOCITY},{0,SEQ_MAX_HUMAN_POSITION},{0,1}};
      for(auto &layer:candidate.mLayers) {
         int fields[16];
         for(unsigned i=0;i<16;++i)if(!in.signed32(fields[i],layerBounds[i][0],layerBounds[i][1]))return false;
         layer.mNumRows=fields[0];layer.mNumSteps=fields[1];layer.mIsMonoMode=fields[2]!=0;layer.mMaxPoly=fields[3];
         layer.mPolyBias=fields[4];layer.mCurrentNoteSet=static_cast<SequenceLayer::WhichNoteSet>(fields[5]);
         layer.mCurrentPattern=fields[6];layer.mClockDivider=fields[7];layer.mMidiChannel=(int8_t)fields[8];
         layer.mDutyCycle=fields[9];layer.mStepsPerMeasure=fields[10];layer.mMuted=fields[11]!=0;
         layer.mHumanLen=fields[12];layer.mHumanVelo=fields[13];layer.mHumanPos=fields[14];layer.mCombineMode=fields[15]!=0;
         unsigned octave;
         if(!in.string(layer.mLayerName)||!in.string(layer.mStdKeyName)||!in.string(layer.mStdScaleName)||
            !in.small(1,SEQ_NUM_OCTAVES-1,octave))return false;
         layer.mStdOctave=(int)octave;
         bool key=false,scale=false;
         for(int i=0;i<SeqScale::getNumNoteNames();++i)key|=std::strcmp(layer.mStdKeyName,SeqScale::getNoteName(i))==0;
         for(int i=0;i<SeqScale::getNumScales();++i)scale|=std::strcmp(layer.mStdScaleName,SeqScale::getScaleName(i))==0;
         if(!key||!scale)return false;
         for(int row=0;row<SEQ_MAX_ROWS;++row) {
            auto &standard=layer.mNoteSets[SequenceLayer::standardSet].notes[row];
            auto &custom=layer.mNoteSets[SequenceLayer::customSet].notes[row];
            if(!in.byte(standard.note,-1,127)||!in.string(standard.noteName)||
               !in.byte(custom.note,-1,127)||!in.string(custom.noteName))return false;
         }
         for(auto &pattern:layer.mPats) {
            if(!in.string(pattern.mName))return false;
            unsigned rows;if(!in.small(2,SEQ_MAX_ROWS,rows))return false;
            int previousRow=-1;
            for(unsigned r=0;r<rows;++r) {
               unsigned row,runs;
               if(!in.small(1,SEQ_MAX_ROWS-1,row)||(int)row<=previousRow||!in.small(1,SEQ_MAX_STEPS,runs)||!runs)return false;
               previousRow=(int)row;unsigned column=0;bool meaningful=false;
               SequenceLayer::Cell previous;
               for(unsigned run=0;run<runs;++run) {
                  unsigned length;SequenceLayer::Cell cell;
                  if(!in.small(1,SEQ_MAX_STEPS,length)||!length||length>SEQ_MAX_STEPS-column||
                     !in.byte(cell.prob,SEQ_PROB_OFF,SEQ_PROB_ON)||!in.byte(cell.velo,0,127)||
                     !in.byte(cell.length,-(SEQ_MAX_RETRIGGER-1),127)||!in.byte(cell.offset,-50,50))return false;
                  if(run&&previous.prob==cell.prob&&previous.velo==cell.velo&&previous.length==cell.length&&previous.offset==cell.offset)return false;
                  meaningful|=cell.prob!=SEQ_PROB_OFF||cell.velo||cell.length||cell.offset;
                  for(unsigned i=0;i<length;++i)pattern.mRows[row].mSteps[column++]=cell;
                  previous=cell;
               }
               if(column!=SEQ_MAX_STEPS||!meaningful)return false;
            }
            unsigned columns;if(!in.small(1,SEQ_MAX_STEPS,columns))return false;
            int previousColumn=-1;
            for(unsigned c=0;c<columns;++c) {
               unsigned column,count;
               if(!in.small(1,SEQ_MAX_STEPS-1,column)||(int)column<=previousColumn||
                  !in.small(1,SEQ_MAX_CHAIN_SOURCES,count)||!count)return false;
               previousColumn=(int)column;
               auto &pool=pattern.mChains[column];
               for(unsigned i=0;i<count;++i) {
                  unsigned sourceColumn,sourceRow,targetRow,flags;
                  if(!in.small(1,SEQ_MAX_STEPS-1,sourceColumn)||!in.small(1,SEQ_MAX_ROWS-1,sourceRow)||
                     !in.small(1,SEQ_MAX_ROWS-1,targetRow)||!in.small(1,7,flags)||!(flags&SEQ_CHAIN_FLAG_USED))return false;
                  auto &link=pool.cells[i];link.col=(uint8_t)sourceColumn;link.row=(uint8_t)sourceRow;
                  link.targetrow=(uint8_t)targetRow;link.flags=(int8_t)flags;
                  if(i) {
                     const auto &previous=pool.cells[i-1];
                     if(std::tie(previous.targetrow,previous.row,previous.col)>=std::tie(link.targetrow,link.row,link.col))return false;
                  }
               }
            }
         }
      }
      return in.end();
   }
};
