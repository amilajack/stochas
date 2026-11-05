// SPDX-License-Identifier: GPL-3.0-only
#include "SequenceDelta.h"
#include "Persist.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
using namespace Stochas::Wclap;
namespace {
template<class T> bool same(const T &a, const T &b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
void string(Protocol::Writer &out, const char *text) {
   const auto size = (uint16_t)std::strlen(text);
   out.put(size).append(text, size);
}
template<size_t N> bool string(Protocol::Reader &in, char (&out)[N]) {
   uint16_t size; const uint8_t *bytes;
   if (!in.get(size) || size >= N || !in.getBytes(size, bytes) ||
       std::memchr(bytes, 0, size) || !juce::CharPointer_UTF8::isValidString((const char*)bytes, size)) return false;
   std::memset(out, 0, N); std::memcpy(out, bytes, size); return true;
}
bool range(int value, int low, int high) { return value >= low && value <= high; }
}

SeqWireDelta::Encoded SeqWireDelta::encode(const SequenceData &before, const SequenceData &after, uint64_t revision) {
   if (revision == std::numeric_limits<uint64_t>::max()) return {{}, true};
   Protocol::Writer out(Protocol::Delta, 4096);
   out.put(revision).put(revision + 1).put(uint32_t{0});
   uint32_t count = 0;
   const auto op = [&](Protocol::Operation type) { ++count; out.put(uint8_t(type)); };
   // Global field IDs: swing, passthrough, respond, offset, playback, seed, BPM.
   const int oldGlobals[]{before.mSwing,before.mMidiPassthru,before.mMidiRespond,before.mOffsetTime,before.mAutoPlay};
   const int newGlobals[]{after.mSwing,after.mMidiPassthru,after.mMidiRespond,after.mOffsetTime,after.mAutoPlay};
   for (uint8_t i = 0; i < 5; ++i) if(oldGlobals[i] != newGlobals[i]) {
      op(Protocol::SetGlobal); out.put(i).put(int32_t(newGlobals[i]));
   }
   if (before.mRandomSeed != after.mRandomSeed) { op(Protocol::SetGlobal); out.put(uint8_t{5}).put(int64_t(after.mRandomSeed)); }
   if (before.mStandaloneBPM != after.mStandaloneBPM) { op(Protocol::SetGlobal); out.put(uint8_t{6}).put(after.mStandaloneBPM); }
   for(uint8_t i=0;i<SEQ_DEFAULT_NUM_STEPS;++i) if(before.mGroove[i]!=after.mGroove[i]) {
      op(Protocol::SetGroove);out.put(i).put(int8_t(after.mGroove[i]));
   }
   if(before.mMidiMapCount!=after.mMidiMapCount || !same(before.mMidiMap,after.mMidiMap)) {
      op(Protocol::ReplaceMappings);out.put(uint16_t(after.mMidiMapCount));
      for(const auto &m:after.mMidiMap) out.put(m.mAction).put(m.mTarget).put(m.mValue).put(m.mType).put(m.mNote).put(m.mChannel);
   }
   for(uint8_t l=0;l<SEQ_MAX_LAYERS;++l) {
      const auto &a=before.mLayers[l], &b=after.mLayers[l];
      if(same(a,b))continue;
      const int x[]{a.mNumRows,a.mNumSteps,a.mIsMonoMode,a.mMaxPoly,a.mPolyBias,a.mCurrentNoteSet,
         a.mCurrentPattern,a.mClockDivider,a.mMidiChannel,a.mDutyCycle,a.mStepsPerMeasure,
         a.mMuted,a.mHumanLen,a.mHumanVelo,a.mHumanPos,a.mCombineMode};
      const int y[]{b.mNumRows,b.mNumSteps,b.mIsMonoMode,b.mMaxPoly,b.mPolyBias,b.mCurrentNoteSet,
         b.mCurrentPattern,b.mClockDivider,b.mMidiChannel,b.mDutyCycle,b.mStepsPerMeasure,
         b.mMuted,b.mHumanLen,b.mHumanVelo,b.mHumanPos,b.mCombineMode};
      for(uint8_t field=0;field<16;++field)if(x[field]!=y[field]) {
         op(Protocol::SetLayer);out.put(l).put(field).put(int32_t(y[field]));
      }
      if(std::strcmp(a.mLayerName,b.mLayerName)) {op(Protocol::SetLayerName);out.put(l);string(out,b.mLayerName);}
      if(a.mStdOctave!=b.mStdOctave || std::strcmp(a.mStdKeyName,b.mStdKeyName) || std::strcmp(a.mStdScaleName,b.mStdScaleName)) {
         op(Protocol::SetScale);out.put(l).put(uint8_t(b.mStdOctave));string(out,b.mStdKeyName);string(out,b.mStdScaleName);
      }
      for(uint8_t r=0;r<SEQ_MAX_ROWS;++r) {
         const auto &oldCustom=a.mNoteSets[SequenceLayer::customSet].notes[r], &custom=b.mNoteSets[SequenceLayer::customSet].notes[r];
         const auto standard=b.mNoteSets[SequenceLayer::standardSet].notes[r].note;
         if(!same(oldCustom,custom) || a.mNoteSets[SequenceLayer::standardSet].notes[r].note!=standard) {
            op(Protocol::SetNotes);out.put(l).put(r).put(standard).put(custom.note);string(out,custom.noteName);
         }
      }
      for(uint8_t p=0;p<SEQ_MAX_PATTERNS;++p) {
         const auto &old=a.mPats[p], &now=b.mPats[p];
         if(same(old,now))continue;
         if(std::strcmp(old.mName,now.mName)) {op(Protocol::SetPatternName);out.put(l).put(p);string(out,now.mName);}
         for(uint8_t r=0;r<SEQ_MAX_ROWS;++r) {
            if(same(old.mRows[r],now.mRows[r]))continue;
            for(uint8_t c=0;c<SEQ_MAX_STEPS;++c)if(!same(old.mRows[r].mSteps[c],now.mRows[r].mSteps[c])) {
               if(count==Protocol::maxDeltaOperations)return {{},true};
               const auto &cell=now.mRows[r].mSteps[c];
               op(Protocol::SetCell);out.put(l).put(p).put(r).put(c).put(cell.prob).put(cell.velo).put(cell.length).put(cell.offset);
            }
         }
         for(uint8_t c=0;c<SEQ_MAX_STEPS;++c)if(!same(old.mChains[c],now.mChains[c])) {
            if(count==Protocol::maxDeltaOperations)return {{},true};
            uint8_t used=0;for(const auto &link:now.mChains[c].cells)if(link.flags&SEQ_CHAIN_FLAG_USED)++used;
            op(Protocol::ReplaceChainColumn);out.put(l).put(p).put(c).put(used);
            for(const auto &link:now.mChains[c].cells)if(link.flags&SEQ_CHAIN_FLAG_USED)
               out.put(link.col).put(link.row).put(link.targetrow).put(link.flags);
         }
      }
      if(count>Protocol::maxDeltaOperations)return {{},true};
   }
   if(count>Protocol::maxDeltaOperations)return {{},true};
   if(!count)return {};
   std::memcpy(out.bytes.data()+20,&count,sizeof(count));
   return {std::move(out.bytes),false};
}

std::unique_ptr<SequenceData> SeqWireDelta::prepare(const SequenceData &current,const void *frame,size_t size,uint64_t expected,uint64_t &next) {
   auto prepared=std::make_unique<SequenceData>(current);
   if(!decode(*prepared,frame,size,expected,next))return {};
   return prepared;
}
bool SeqWireDelta::applyPrepared(SequenceData &staging,const SequenceData &current,const void *frame,size_t size,uint64_t expected,uint64_t &next) {
   if(&staging==&current)return false;
   if(decode(staging,frame,size,expected,next))return true;
   staging=current;return false;
}
bool SeqWireDelta::decode(SequenceData &staging,const void *frame,size_t size,uint64_t expected,uint64_t &next) {
   Protocol::Reader in(frame,size);uint64_t revision,wanted;uint32_t count;
   if(!in.valid() || in.type()!=Protocol::Delta || !in.get(revision) || !in.get(wanted) ||
      !in.get(count) || revision!=expected || revision==std::numeric_limits<uint64_t>::max() ||
      wanted!=revision+1 || !count || count>Protocol::maxDeltaOperations)return {};
   auto *result=&staging;
   for(uint32_t operation=0;operation<count;++operation) {
      uint8_t type;if(!in.get(type))return {};
      if(type==Protocol::SetGlobal) {
         uint8_t field;if(!in.get(field))return {};
         if(field==5) {int64_t value;if(!in.get(value))return {};result->mRandomSeed=value;}
         else if(field==6) {double value;if(!in.get(value)||!std::isfinite(value)||value<1||value>300)return {};result->mStandaloneBPM=value;}
         else {
            int32_t value;if(!in.get(value))return {};
            switch(field) {
            case 0:if(!range(value,-50,50))return {};result->mSwing=value;break;
            case 1:if(!range(value,SEQ_MIDI_PASSTHRU_NONE,SEQ_MIDI_PASSTHRU_ALL))return {};result->mMidiPassthru=value;break;
            case 2:if(!range(value,0,1))return {};result->mMidiRespond=value;break;
            case 3:if(!range(value,SEQ_POS_OFFSET_MIN,SEQ_POS_OFFSET_MAX))return {};result->mOffsetTime=value;break;
            case 4:if(!range(value,SEQ_PLAYMODE_AUTO,SEQ_PLAYMODE_MEASURE))return {};result->mAutoPlay=value;break;
            default:return {};
            }
         }
      } else if(type==Protocol::SetGroove) {
         uint8_t index;int8_t value;if(!in.get(index)||!in.get(value)||index>=SEQ_DEFAULT_NUM_STEPS||!range(value,-50,50))return {};
         result->mGroove[index]=value;
      } else if(type==Protocol::ReplaceMappings) {
         uint16_t length;if(!in.get(length)||length>SEQMIDI_MAX_ITEMS)return {};
         for(auto &m:result->mMidiMap) {
            if(!in.get(m.mAction)||!in.get(m.mTarget)||!in.get(m.mValue)||!in.get(m.mType)||!in.get(m.mNote)||!in.get(m.mChannel)||!SeqPersist::validMapping(m))return {};
         }
         result->mMidiMapCount=length;
      } else {
         uint8_t l;if(!in.get(l)||l>=SEQ_MAX_LAYERS)return {};
         auto &layer=result->mLayers[l];
         if(type==Protocol::SetLayer) {
            uint8_t field;int32_t value;if(!in.get(field)||!in.get(value))return {};
            switch(field) {
            case 0:if(!range(value,SEQ_MIN_ROWS,SEQ_MAX_ROWS))return {};layer.mNumRows=value;break;
            case 1:if(!range(value,SEQ_MIN_STEPS,SEQ_MAX_STEPS))return {};layer.mNumSteps=value;break;
            case 2:if(!range(value,0,1))return {};layer.mIsMonoMode=value!=0;break;
            case 3:if(!range(value,1,SEQ_MAX_ROWS))return {};layer.mMaxPoly=value;break;
            case 4:if(!range(value,SEQ_POLY_BIAS_MIN,SEQ_POLY_BIAS_MAX))return {};layer.mPolyBias=value;break;
            case 5:if(!range(value,0,1))return {};layer.mCurrentNoteSet=static_cast<SequenceLayer::WhichNoteSet>(value);break;
            case 6:if(!range(value,0,SEQ_MAX_PATTERNS-1))return {};layer.mCurrentPattern=value;break;
            case 7:if(!range(value,SEQ_MIN_CLOCK_DIV,SEQ_MAX_CLOCK_DIV))return {};layer.mClockDivider=value;break;
            case 8:if(!range(value,1,16))return {};layer.mMidiChannel=(int8_t)value;break;
            case 9:if(!range(value,SEQ_DUTY_MIN,SEQ_DUTY_MAX))return {};layer.mDutyCycle=value;break;
            case 10:if(!range(value,SEQ_MIN_STEPS,SEQ_DEFAULT_NUM_STEPS))return {};layer.mStepsPerMeasure=value;break;
            case 11:if(!range(value,0,1))return {};layer.mMuted=value!=0;break;
            case 12:if(!range(value,0,SEQ_MAX_HUMAN_LENGTH))return {};layer.mHumanLen=value;break;
            case 13:if(!range(value,0,SEQ_MAX_HUMAN_VELOCITY))return {};layer.mHumanVelo=value;break;
            case 14:if(!range(value,0,SEQ_MAX_HUMAN_POSITION))return {};layer.mHumanPos=value;break;
            case 15:if(!range(value,0,1))return {};layer.mCombineMode=value!=0;break;
            default:return {};
            }
         } else if(type==Protocol::SetLayerName) {
            if(!string(in,layer.mLayerName))return {};
         } else if(type==Protocol::SetScale) {
            uint8_t octave;char key[SEQ_KEY_NAME_MAXLEN]{},scale[SEQ_SCALE_NAME_MAXLEN]{};
            if(!in.get(octave)||octave>=SEQ_NUM_OCTAVES||!string(in,key)||!string(in,scale))return {};
            bool keyFound=false,scaleFound=false;
            for(int i=0;i<SeqScale::getNumNoteNames();++i)if(std::strcmp(key,SeqScale::getNoteName(i))==0)keyFound=true;
            for(int i=0;i<SeqScale::getNumScales();++i)if(std::strcmp(scale,SeqScale::getScaleName(i))==0)scaleFound=true;
            if(!keyFound||!scaleFound)return {};
            std::memcpy(layer.mStdKeyName,key,sizeof(key));std::memcpy(layer.mStdScaleName,scale,sizeof(scale));layer.mStdOctave=octave;
         } else if(type==Protocol::SetNotes) {
            uint8_t r;int8_t standard,custom;
            if(!in.get(r)||r>=SEQ_MAX_ROWS||!in.get(standard)||!in.get(custom)||standard < -1||custom < -1)return {};
            auto &note=layer.mNoteSets[SequenceLayer::customSet].notes[r];
            if(!string(in,note.noteName))return {};
            note.note=custom;layer.mNoteSets[SequenceLayer::standardSet].notes[r].note=standard;
         } else {
            uint8_t p;if(!in.get(p)||p>=SEQ_MAX_PATTERNS)return {};
            auto &pattern=layer.mPats[p];
            if(type==Protocol::SetPatternName) {if(!string(in,pattern.mName))return {};}
            else if(type==Protocol::SetCell) {
               uint8_t r,c;int8_t prob,velo,length,offset;
               if(!in.get(r)||!in.get(c)||r>=SEQ_MAX_ROWS||c>=SEQ_MAX_STEPS||!in.get(prob)||!in.get(velo)||!in.get(length)||!in.get(offset)||
                  !range(prob,SEQ_PROB_OFF,SEQ_PROB_ON)||velo<0||!range(length,-(SEQ_MAX_RETRIGGER-1),127)||!range(offset,-50,50))return {};
               auto &cell=pattern.mRows[r].mSteps[c];cell.prob=prob;cell.velo=velo;cell.length=length;cell.offset=offset;
            } else if(type==Protocol::ReplaceChainColumn) {
               uint8_t c,length;if(!in.get(c)||c>=SEQ_MAX_STEPS||!in.get(length)||length>SEQ_MAX_CHAIN_SOURCES)return {};
               auto &column=pattern.mChains[c];column=SequenceLayer::ChainSource{};
               for(unsigned i=0;i<length;++i) {
                  auto &link=column.cells[i];
                  if(!in.get(link.col)||!in.get(link.row)||!in.get(link.targetrow)||!in.get(link.flags)||
                     link.col>=SEQ_MAX_STEPS||link.row>=SEQ_MAX_ROWS||link.targetrow>=SEQ_MAX_ROWS||
                     !(link.flags&SEQ_CHAIN_FLAG_USED)||(link.flags&~7))return {};
                  for(unsigned j=0;j<i;++j)if(column.cells[j].col==link.col&&column.cells[j].row==link.row&&column.cells[j].targetrow==link.targetrow)return {};
               }
            } else return {};
         }
      }
   }
   if(in.remaining())return {};
   next=wanted;return true;
}

void SeqWireDelta::rebase(const SequenceData &base,const SequenceData &local,const SequenceData &remote,SequenceData &result) {
   result=remote;
   const auto field=[](const auto &before,const auto &edited,auto &target) {
      if(!same(before,edited))std::memcpy(&target,&edited,sizeof(target));
   };
#define SEQ_REBASE(member) field(base.member,local.member,result.member)
   SEQ_REBASE(mRandomSeed);SEQ_REBASE(mStandaloneBPM);SEQ_REBASE(mSwing);
   SEQ_REBASE(mMidiPassthru);SEQ_REBASE(mMidiRespond);SEQ_REBASE(mOffsetTime);SEQ_REBASE(mAutoPlay);
   SEQ_REBASE(mMidiMapCount);
   for(int i=0;i<SEQ_DEFAULT_NUM_STEPS;++i)SEQ_REBASE(mGroove[i]);
   for(int i=0;i<SEQMIDI_MAX_ITEMS;++i)SEQ_REBASE(mMidiMap[i]);
   for(int l=0;l<SEQ_MAX_LAYERS;++l) {
#define SEQ_LAYER(member) SEQ_REBASE(mLayers[l].member)
      SEQ_LAYER(mNumRows);SEQ_LAYER(mNumSteps);SEQ_LAYER(mIsMonoMode);SEQ_LAYER(mMaxPoly);
      SEQ_LAYER(mPolyBias);SEQ_LAYER(mCurrentNoteSet);SEQ_LAYER(mCurrentPattern);SEQ_LAYER(mClockDivider);
      SEQ_LAYER(mMidiChannel);SEQ_LAYER(mDutyCycle);SEQ_LAYER(mStepsPerMeasure);SEQ_LAYER(mMuted);
      SEQ_LAYER(mHumanLen);SEQ_LAYER(mHumanVelo);SEQ_LAYER(mHumanPos);SEQ_LAYER(mCombineMode);
      SEQ_LAYER(mLayerName);SEQ_LAYER(mStdKeyName);SEQ_LAYER(mStdScaleName);SEQ_LAYER(mStdOctave);
      for(int set=0;set<SequenceLayer::noteSetCount;++set)for(int row=0;row<SEQ_MAX_ROWS;++row) {
         SEQ_LAYER(mNoteSets[set].notes[row].note);SEQ_LAYER(mNoteSets[set].notes[row].noteName);
      }
      for(int p=0;p<SEQ_MAX_PATTERNS;++p) {
         SEQ_LAYER(mPats[p].mName);
         for(int column=0;column<SEQ_MAX_STEPS;++column)SEQ_LAYER(mPats[p].mChains[column]);
         for(int row=0;row<SEQ_MAX_ROWS;++row)for(int column=0;column<SEQ_MAX_STEPS;++column) {
            SEQ_LAYER(mPats[p].mRows[row].mSteps[column].prob);
            SEQ_LAYER(mPats[p].mRows[row].mSteps[column].velo);
            SEQ_LAYER(mPats[p].mRows[row].mSteps[column].length);
            SEQ_LAYER(mPats[p].mRows[row].mSteps[column].offset);
         }
      }
#undef SEQ_LAYER
   }
#undef SEQ_REBASE
}
