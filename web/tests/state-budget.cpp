// SPDX-License-Identifier: GPL-3.0-only
// Explicit state-size/save assessment; not a playback or shipping qualification.
#include "PluginProcessor.h"
#include "../proposals/CompactStatePrototype.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>
extern "C" bool stochas_oracle_background_only();
extern "C" bool stochas_oracle_never_frontmost();
namespace {
using Clock=std::chrono::steady_clock;
volatile uint8_t sink=0;
void fill(SequenceData &state,int kind) {
   state.setRandomSeed(42);state.clearMapping();state.setMappingCount(0);
   std::mt19937 random(1234);
   for(int l=0;l<SEQ_MAX_LAYERS;++l) {
      auto *layer=state.getLayer(l);layer->setMuted(true);layer->setMonoMode(false);
      layer->setNumSteps(64);layer->setMaxRows(129);layer->setMaxPoly(128);layer->setNoteSource(true);
      for(int r=0;r<SEQ_MAX_ROWS;++r)layer->setNote(r,(int8_t)(r==128?-1:r),true);
      if(!kind)continue;
      for(int p=0;p<SEQ_MAX_PATTERNS;++p) {
         for(int r=0;r<SEQ_MAX_ROWS;++r)for(int c=0;c<SEQ_MAX_STEPS;++c) {
            layer->setProb(r,c,(int8_t)(kind==1?100:random()%101),p);
            layer->setVel(r,c,(int8_t)(kind==1?100:1+random()%127),p);
            layer->setLength(r,c,(int8_t)(kind==1?0:int(random()%71)-7),p);
            layer->setOffset(r,c,(int8_t)(kind==1?0:int(random()%101)-50),p);
         }
         if(kind==2)for(int c=0;c<SEQ_MAX_STEPS;++c)for(int link=0;link<SEQ_MAX_CHAIN_SOURCES;++link)
            if(!layer->addChainSource(link,c,(link+1)%128,(c+63)%64,link%2,(link/2)%2,p))throw std::runtime_error("legal chain fixture refused");
      }
   }
}
template<class Fn> double measure(Fn &&fn) {
   const auto start=Clock::now();fn();
   return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
void write(const std::filesystem::path &path,const void *data,size_t size) {
   std::ofstream out(path,std::ios::binary);out.write((const char*)data,size);
   if(!out)throw std::runtime_error("fixture write failed");
}
}
int main(int argc,char **argv) {
   try {
      if(!stochas_oracle_background_only()||argc!=3)throw std::runtime_error("usage: state-budget --prepare|--measure OUTPUT_DIR");
      const char *config=std::getenv("STOCHAS_CONFIG_ROOT");
      if(!config||!juce::File::isAbsolutePath(config))throw std::runtime_error("task-owned absolute config required");
      const bool timing=std::string(argv[1])=="--measure";
      if(!timing&&std::string(argv[1])!="--prepare")throw std::runtime_error("unknown mode");
      std::filesystem::create_directories(argv[2]);juce::ScopedJuceInitialiser_GUI gui;
      for(int kind=0;kind<3;++kind) {
         auto processor=std::make_unique<SeqAudioProcessor>();
         fill(*processor->mData.getUISeqData(),kind);processor->mData.swap();
         auto &audio=static_cast<juce::AudioProcessor&>(*processor);
         juce::MemoryBlock xml;audio.getStateInformation(xml);
         std::vector<uint8_t> compact;compact.reserve(StochasCompactStatePrototype::MaximumBytes);
         StochasCompactStatePrototype::encode(*processor->mData.getPublishedSeqData(),compact);
         const char *names[]{"sparse","dense-uniform","dense-random-chains"};
         const std::filesystem::path root(argv[2]);
         if(!timing) {
            write(root/(std::string(names[kind])+".state"),xml.getData(),xml.getSize());
            write(root/(std::string(names[kind])+".prototype"),compact.data(),compact.size());
         }
         std::cout<<"{\"case\":\""<<names[kind]<<"\",\"modelBytes\":"<<sizeof(SequenceData)
                  <<",\"xmlBytes\":"<<xml.getSize()<<",\"prototypeBytes\":"<<compact.size()
                  <<",\"prototypeProvenMaximumBytes\":"<<StochasCompactStatePrototype::ProvenMaximumBytes;
         if(timing) {
            std::cout<<",\"xmlSaveMs\":[";
            for(int i=0;i<3;++i){if(i)std::cout<<',';std::cout<<measure([&]{audio.getStateInformation(xml);});}
            std::cout<<"],\"prototypeFullEncodeMs\":[";
            for(int i=0;i<3;++i){if(i)std::cout<<',';std::cout<<measure([&]{StochasCompactStatePrototype::encode(*processor->mData.getPublishedSeqData(),compact);});}
            std::vector<uint8_t> source(std::max<size_t>(xml.getSize(),2*1024*1024),123),destination(source.size());
            std::cout<<"],\"preparedCopyMs\":[";bool first=true;
            for(size_t size:{size_t(65536),size_t(262144),sizeof(SequenceData),size_t(2*1024*1024),xml.getSize()}) {
               if(!first)std::cout<<',';first=false;
               const auto milliseconds=measure([&]{for(int i=0;i<20;++i){std::memcpy(destination.data(),source.data(),size);asm volatile(""::"r"(destination.data()):"memory");sink^=destination[(i*4096)%size];}})/20;
               std::cout<<"{\"bytes\":"<<size<<",\"ms\":"<<milliseconds<<'}';
            }
            std::cout<<']';
         }
         std::cout<<"}\n";
      }
      if(!stochas_oracle_never_frontmost())throw std::runtime_error("task unexpectedly frontmost");
      return 0;
   } catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
