// SPDX-License-Identifier: GPL-3.0-only
// Test/authoring utility: original XML files remain the native preset format;
// the unshipped WCLAP runtime uses the portable compact model representation.
#include "PluginProcessor.h"
#include "../wclap/CompactState.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
extern "C" bool stochas_oracle_background_only();
extern "C" bool stochas_oracle_never_frontmost();
int main(int argc,char **argv) {
   try {
      if(argc!=4||!stochas_oracle_background_only())throw std::runtime_error("usage: state-convert --from-xml|--to-xml INPUT OUTPUT");
      const char *config=std::getenv("STOCHAS_CONFIG_ROOT");
      if(!config||!juce::File::isAbsolutePath(config))throw std::runtime_error("isolated absolute config required");
      juce::ScopedJuceInitialiser_GUI gui;
      std::ifstream input(argv[2],std::ios::binary|std::ios::ate);
      if(!input||input.tellg()<=0||input.tellg()>64*1024*1024)throw std::runtime_error("invalid input size");
      const auto size=(size_t)input.tellg();input.seekg(0);
      std::vector<uint8_t> bytes(size);input.read((char*)bytes.data(),(std::streamsize)size);
      if(!input)throw std::runtime_error("input read failed");
      auto processor=std::make_unique<SeqAudioProcessor>();
      auto defaults=std::make_unique<SequenceData>();
      std::vector<uint8_t> encoded;encoded.reserve(SeqCompactState::MaximumBytes);
      juce::MemoryBlock xml;
      const void *data=nullptr;size_t outputSize=0;
      if(std::string(argv[1])=="--from-xml") {
         if(!processor->loadCanonicalState(bytes.data(),(int)bytes.size()))throw std::runtime_error("original XML state refused");
         SeqCompactState::encode(*processor->mData.getPublishedSeqData(),encoded);
         data=encoded.data();outputSize=encoded.size();
      } else if(std::string(argv[1])=="--to-xml") {
         if(!SeqCompactState::decode(*processor->mData.getUISeqData(),*defaults,bytes.data(),bytes.size()))
            throw std::runtime_error("compact state refused");
         processor->commitPreparedState();
         static_cast<juce::AudioProcessor&>(*processor).getStateInformation(xml);
         data=xml.getData();outputSize=xml.getSize();
      } else throw std::runtime_error("unknown conversion mode");
      std::ofstream output(argv[3],std::ios::binary);output.write((const char*)data,(std::streamsize)outputSize);
      if(!output)throw std::runtime_error("output write failed");
      if(!stochas_oracle_never_frontmost())throw std::runtime_error("task unexpectedly frontmost");
      return 0;
   } catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
