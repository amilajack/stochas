// SPDX-License-Identifier: GPL-3.0-only
#include "../wclap/SequenceDelta.h"
#include "../wclap/CompactState.h"
#include "Persist.h"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
extern "C" void stochas_audit_begin();
extern "C" void stochas_audit_end(uint64_t*,uint64_t*,uint64_t*);
namespace {
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
juce::MemoryBlock canonical(SequenceData &value) {
   SeqPersist persist;juce::MemoryBlock result;
   juce::AudioProcessor::copyXmlToBinary(persist.store(&value),result);return result;
}
}
void stochas_test_sequence_delta() {
   {
      std::vector<uint8_t> packet;packet.reserve(64);const auto *storage=packet.data();
      const auto capacity=packet.capacity();std::vector<uint8_t> excess(capacity+1);
      uint64_t allocations=0,frees=0,locks=0;bool valid=true;
      stochas_audit_begin();
      for(unsigned i=0;i<256;++i) {
         Stochas::Wclap::Protocol::Writer writer(Stochas::Wclap::Protocol::Telemetry,packet);
         writer.put(uint64_t(i)).put(double(i));valid&=writer.valid()&&writer.bytes.size()==20;
      }
      {
         Stochas::Wclap::Protocol::Writer writer(Stochas::Wclap::Protocol::Telemetry,packet);
         writer.append(excess.data(),excess.size());
         valid&=!writer.valid()&&writer.bytes.size()==4;
      }
      stochas_audit_end(&allocations,&frees,&locks);
      check(valid&&packet.capacity()==capacity&&packet.data()==storage&&!allocations&&!frees&&!locks,
            "prepared webview frame grew/lost storage or allocated/freed/locked on reuse/refusal");
   }
   auto before=std::make_unique<SequenceData>(),after=std::make_unique<SequenceData>();
   auto defaults=std::make_unique<SequenceData>(),compactCandidate=std::make_unique<SequenceData>();
   {
      auto local=std::make_unique<SequenceData>(*before),remote=std::make_unique<SequenceData>(*before),merged=std::make_unique<SequenceData>();
      local->getLayer(0)->setVel(0,0,91,0);remote->getLayer(0)->setProb(0,0,73,0);
      remote->getLayer(1)->setProb(128,63,100,7);remote->setSwing(17);
      SeqWireDelta::rebase(*before,*local,*remote,*merged);
      check(merged->getLayer(0)->getVel(0,0,0)==91&&merged->getLayer(0)->getProb(0,0,0)==73&&
            merged->getLayer(1)->getProb(128,63,7)==100&&merged->getSwing()==17,
            "editor resync lost local edits or concurrent remote recording fields");
   }
   std::vector<uint8_t> compact,reencoded;
   compact.reserve(SeqCompactState::MaximumBytes);reencoded.reserve(SeqCompactState::MaximumBytes);
   const auto compactRoundtrip=[&](SequenceData &source) {
      uint64_t allocations=0,frees=0,locks=0;
      stochas_audit_begin();
      SeqCompactState::encode(source,compact);
      const bool decoded=SeqCompactState::decode(*compactCandidate,*defaults,compact.data(),compact.size());
      if(decoded)SeqCompactState::encode(*compactCandidate,reencoded);
      stochas_audit_end(&allocations,&frees,&locks);
      check(decoded&&reencoded==compact,"compact state canonical roundtrip changed fields");
      check(!allocations&&!frees&&!locks,"prepared compact codec allocated/freed/locked");
      check(canonical(source)==canonical(*compactCandidate),"compact runtime state changed original XML preset fields");
   };
   compactRoundtrip(*before);
   auto invalidCompact=compact;
   for(size_t at:{size_t(0),size_t(4),size_t(6),size_t(8),size_t(9),size_t(10),size_t(12),size_t(30),size_t(68+3),size_t(668)}) {
      invalidCompact=compact;invalidCompact[at]=255;
      check(!SeqCompactState::decode(*compactCandidate,*defaults,invalidCompact.data(),invalidCompact.size()),"malformed compact header/value accepted");
   }
   invalidCompact=compact;
   for(size_t at=22;at<30;++at)invalidCompact[at]=255;
   check(!SeqCompactState::decode(*compactCandidate,*defaults,invalidCompact.data(),invalidCompact.size()),"nonfinite compact BPM accepted");
   for(size_t length=0;length<compact.size();length+=std::max<size_t>(1,compact.size()/37))
      check(!SeqCompactState::decode(*compactCandidate,*defaults,compact.data(),length),"truncated compact state accepted");
   check(!SeqCompactState::decode(*compactCandidate,*defaults,compact.data(),compact.size()-1),"last-byte compact truncation accepted");
   invalidCompact=compact;invalidCompact.push_back(0);
   check(!SeqCompactState::decode(*compactCandidate,*defaults,invalidCompact.data(),invalidCompact.size()),"trailing compact state byte accepted");
   {
      auto *layer=after->getLayer(0);
      layer->setLayerName("1234567890123\xc3\xa9");
      layer->setPatternName("1234567890123\xc3\xa9");
      layer->setNoteName(0,"123456789012345\xc3\xa9");
      check(std::strcmp(layer->getLayerName(),"1234567890123")==0&&
            std::strcmp(layer->getPatternName(),"1234567890123")==0&&
            std::strcmp(layer->getNoteName(0),"123456789012345")==0,"UTF-8 byte limit split a label code point");
      layer->setNoteName(0,layer->getNoteName(0));
      check(std::strcmp(layer->getNoteName(0),"123456789012345")==0,"self-renaming erased note label");
      compactRoundtrip(*after);
   }
   *after=*before;
   after->getLayer(0)->setProb(0,0,100,0);
   auto one=SeqWireDelta::encode(*before,*after,9);
   check(!one.needsSnapshot && one.bytes.size()==33,"one-cell drag did not use the compact semantic delta");
   uint64_t next=0;
   auto decoded=SeqWireDelta::prepare(*before,one.bytes.data(),one.bytes.size(),9,next);
   check(decoded && next==10 && canonical(*decoded)==canonical(*after),"one-cell delta changed unrelated fields");
   for(size_t length=0;length<one.bytes.size();++length)
      check(!SeqWireDelta::prepare(*before,one.bytes.data(),length,9,next),"truncated delta accepted");
   check(!SeqWireDelta::prepare(*before,one.bytes.data(),one.bytes.size(),8,next),"stale revision accepted");
   auto invalid=one.bytes;invalid[25]=255;
   check(!SeqWireDelta::prepare(*before,invalid.data(),invalid.size(),9,next),"invalid layer accepted");
   invalid=one.bytes;invalid.back()=127;
   check(!SeqWireDelta::prepare(*before,invalid.data(),invalid.size(),9,next),"invalid cell offset accepted");
   invalid=one.bytes;invalid.push_back(0);
   check(!SeqWireDelta::prepare(*before,invalid.data(),invalid.size(),9,next),"trailing bytes accepted");
   invalid=one.bytes;invalid[1]=1;
   check(!SeqWireDelta::prepare(*before,invalid.data(),invalid.size(),9,next),"reserved header bytes accepted");
   check(SeqWireDelta::encode(*before,*after,std::numeric_limits<uint64_t>::max()).needsSnapshot,"revision overflow wrapped");
   std::mt19937 random(9876);
   for(int trial=0;trial<40;++trial) {
      *after=*before;
      after->setRandomSeed(std::numeric_limits<int64_t>::max()-trial);
      after->setStandaloneBPM(123.456+trial);after->setOffsetTime(-500+trial);
      after->setSwing(trial-20);after->setMidiPassthru(3);after->setMidiRespond(0);
      after->setAutoPlayMode(SEQ_PLAYMODE_MEASURE);after->setGroove(trial%16,trial-20);
      auto *layer=after->getLayer(trial%SEQ_MAX_LAYERS);
      layer->setMaxRows(SEQ_MAX_ROWS);layer->setMonoMode(false);layer->setCombineMode(true);
      layer->setMaxPoly(128);layer->setPolyBias(trial-20);layer->setClockDivider(64);
      layer->setMidiChannel(16);layer->setStepsPerMeasure(16);layer->setDutyCycle(101);
      layer->setHumanLength(100);layer->setHumanVelocity(100);layer->setHumanPosition(50);
      layer->setMuted(true);layer->setNoteSource(true);layer->setCurrentPattern(7);
      layer->setLayerName("Delta layer");layer->setPatternName("Delta pattern",7);
      layer->setNote(128,127,true);layer->setNote(128,0,false);layer->setNoteName(128,"sixteen-byte-tag!");
      const char *scale,*key;int octave;
      layer->getKeyScaleOct(&scale,&key,&octave);layer->setKeyScaleOct(scale,key,(octave+1)%SEQ_NUM_OCTAVES);
      for(int edit=0;edit<100;++edit) {
         const int row=random()%SEQ_MAX_ROWS,step=random()%SEQ_MAX_STEPS,pattern=random()%SEQ_MAX_PATTERNS;
         layer->setProb(row,step,(int8_t)(random()%102-1),pattern);
         layer->setVel(row,step,(int8_t)(random()%128),pattern);
         layer->setLength(row,step,(int8_t)(random()%135-7),pattern);
         layer->setOffset(row,step,(int8_t)(random()%101-50),pattern);
      }
      layer->addChainSource(0,1,128,63,true,false,7);
      layer->addChainSource(128,1,0,0,false,true,7);
      after->setMappingCount(SEQMIDI_MAX_ITEMS);
      for(int i=0;i<SEQMIDI_MAX_ITEMS;++i) {
         auto *mapping=after->getMappingItem(i);
         *mapping=SeqMidiMapItem(SEQMIDI_ACTION_CHGPAT,SEQMIDI_TARGET_ALL,1,SEQ_MIDI_NOTEON,127,1);
      }
      auto frame=SeqWireDelta::encode(*before,*after,19);
      check(!frame.needsSnapshot && !frame.bytes.empty(),"ordinary structural edit unexpectedly requires snapshot");
      auto candidate=SeqWireDelta::prepare(*before,frame.bytes.data(),frame.bytes.size(),19,next);
      check(candidate && next==20 && canonical(*candidate)==canonical(*after),"semantic delta lost model fields/notes/chains/mappings");
      compactRoundtrip(*after);
      *before=*after;
   }
   // A large legal operation uses canonical resync; it cannot be silently
   // truncated into a partial transaction to fit the live-edit packet budget.
   for(int layer=0;layer<SEQ_MAX_LAYERS;++layer)
      for(int pattern=0;pattern<SEQ_MAX_PATTERNS;++pattern)
         for(int row=0;row<SEQ_MAX_ROWS;++row)
            for(int step=0;step<SEQ_MAX_STEPS;++step)
               after->getLayer(layer)->setProb(row,step,100,pattern);
   const auto full=SeqWireDelta::encode(*before,*after,20);
   check(full.needsSnapshot && full.bytes.empty(),"oversized legal edit was partially encoded");
   compactRoundtrip(*after);
   // Runtime state retains even inactive mapping rows, unlike XML's visible
   // row list. Re-enabling them must recover the exact original actions.
   after->setMappingCount(0);
   compactRoundtrip(*after);
   check(compactCandidate->getMappingItem(99)->mNote==127,"inactive mapping row was lost");
   std::cout<<"PASS prepared compact state:all-field/UTF-8/disabled/dense/inactive-map roundtrips,strict malformed controls,zero allocation/free/mutex\n";
   std::cout<<"PASS structural deltas:33-byte cell drag,40 randomized complete-model transactions,strict bounds/revision/truncation controls\n";
}
