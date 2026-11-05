// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "EditorState.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <stdexcept>
extern "C" bool stochas_oracle_background_only();
extern "C" bool stochas_oracle_never_frontmost();
extern "C" void stochas_audit_begin();
extern "C" void stochas_audit_end(uint64_t*, uint64_t*, uint64_t*);
extern void stochas_test_sequence_delta();
extern void stochas_test_file_import();
static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct ResetListener final : juce::AudioProcessorParameter::Listener {
   int values = 0, gestures = 0;
   void parameterValueChanged(int, float) override { ++values; }
   void parameterGestureChanged(int, bool) override { ++gestures; }
};
class PlayHead final : public juce::AudioPlayHead {
public:
   int64_t frame = 0; bool playing = true;
   juce::Optional<PositionInfo> getPosition() const override {
      PositionInfo position;
      position.setTimeInSamples(frame); position.setTimeInSeconds(frame / 48000.0);
      position.setPpqPosition(frame / 24000.0); position.setBpm(120.0);
      position.setTimeSignature(TimeSignature{4,4}); position.setIsPlaying(playing);
      return position;
   }
};
int main() {
   try {
      require(stochas_oracle_background_only(), "background-only policy unavailable");
      const char *root = std::getenv("STOCHAS_CONFIG_ROOT");
      require(root && juce::File::isAbsolutePath(root), "isolated absolute config root required");
      // Prove the instrumentation catches real allocation/deallocation/locks.
      uint64_t a = 0, r = 0, l = 0;
      stochas_audit_begin();
      void *volatile memory = std::malloc(4096);
      pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
      pthread_mutex_lock(&mutex); pthread_mutex_unlock(&mutex);
      std::free(memory);
      stochas_audit_end(&a, &r, &l);
      require(a > 0 && r > 0 && l > 0, "allocation/lock negative control did not fire");
      pthread_mutex_destroy(&mutex);
      juce::ScopedJuceInitialiser_GUI gui;
      stochas_test_sequence_delta();
      stochas_test_file_import();
      {
         // Turning a cell off must not erase its edited attributes or chain.
         // Include a chain-only target at the last row/column, plus a plain
         // inactive cell, so saving cannot rely solely on enabled probability.
         auto original = std::make_unique<SeqAudioProcessor>();
         auto *pattern = original->mData.getUISeqData()->getLayer(3);
         pattern->setVel(127, 62, 83, 7);
         pattern->setLength(127, 62, -3, 7);
         pattern->setOffset(127, 62, -41, 7);
         require(pattern->addChainSource(128, 63, 127, 62, true, false, 7), "inactive chain fixture refused");
         require(pattern->addChainSource(128, 63, 126, 61, false, true, 7), "second inactive chain fixture refused");
         original->mData.swap();
         juce::MemoryBlock saved, reloaded;
         static_cast<juce::AudioProcessor&>(*original).getStateInformation(saved);
         auto restored = std::make_unique<SeqAudioProcessor>();
         require(restored->loadCanonicalState(saved.getData(), (int)saved.getSize()), "inactive cell state refused");
         auto *loaded = restored->mData.getPublishedSeqData()->getLayer(3);
         require(loaded->getProb(127, 62, 7) == SEQ_PROB_OFF && loaded->getVel(127, 62, 7) == 83 &&
                 loaded->getLength(127, 62, 7) == -3 && loaded->getOffset(127, 62, 7) == -41,
                 "inactive cell attributes were discarded by state save");
         int index = -1, row = -1, column = -1; bool negativeTarget = false, negativeSource = true;
         bool firstLink = false, secondLink = false;
         while (loaded->getChainSource(128, 63, &index, &row, &column, &negativeTarget, &negativeSource, 7)) {
            firstLink |= row == 127 && column == 62 && negativeTarget && !negativeSource;
            secondLink |= row == 126 && column == 61 && !negativeTarget && negativeSource;
         }
         require(firstLink && secondLink && loaded->getNumChainSources(128, 63, 7) == 2,
                 "inactive chain-only cell fields were discarded by state save");
         static_cast<juce::AudioProcessor&>(*restored).getStateInformation(reloaded);
         require(saved == reloaded, "inactive cell canonical roundtrip changed fields");
      }
      {
         auto buffer = std::make_unique<SeqDataBuffer>();
         const auto original = buffer->getAudSeqData()->getRandomSeed();
         for (int revision = 1; revision <= 100; ++revision) {
            buffer->getUISeqData()->setRandomSeed(revision);
            buffer->swap();
            require(buffer->getPublishedSeqData()->getRandomSeed() == revision, "burst publication lost latest saved state");
            require(buffer->getAudSeqData()->getRandomSeed() == original, "writer changed active audio snapshot");
         }
         require(buffer->beginAudioBlock(), "pending publication was not consumed");
         require(buffer->getAudSeqData()->getRandomSeed() == 100, "consumer did not receive newest burst edit");
         buffer->undo();
         require(buffer->getPublishedSeqData()->getRandomSeed() == 99, "undo after consume lost prior edit");
         buffer->beginAudioBlock();
         require(buffer->getAudSeqData()->getRandomSeed() == 99, "undo publication not observed by audio");
         buffer->undo();
         require(buffer->getPublishedSeqData()->getRandomSeed() == 100, "redo did not retain immutable prior publication");
         auto *mapping = buffer->getUISeqData()->getMappingItem(0);
         mapping->mNote = 127;
         buffer->swap(); buffer->beginAudioBlock();
         require(buffer->getAudioMappingRevision() != 0, "mapping edit did not carry revision to audio boundary");
         std::cout << "Prepared model bytes=" << sizeof(SequenceData)
                   << " publication/undo bytes=" << sizeof(SeqDataBuffer) << '\n';
      }
      auto processor = std::make_unique<SeqAudioProcessor>();
      auto &audio = static_cast<juce::AudioProcessor&>(*processor);
      require(audio.getParameters()[53]->getText(1.0f,32)=="on"&&
              audio.getParameters()[53]->getValueForText("on")==1.0f&&
              audio.getParameters()[53]->getText(0.5f,32)=="off"&&
              audio.getParameters()[53]->getValueForText("off")==0.5f,
              "record automation text/value conversion fell through to mute labels");
      for(int i=0;i<1024;++i)
         require(processor->mNotifier.addCompletedMidiNote(i%128,100,0,i%64),"legal recorded-note burst silently dropped");
      require(!processor->mNotifier.addCompletedMidiNote(0,100,0,0),"full recording queue did not refuse explicitly");
      for(int i=0;i<1024;++i) {
         int note,velocity,length,position;
         require(processor->mNotifier.getCompletedMidiNote(&note,&velocity,&length,&position)&&
            note==i%128&&velocity==100&&length==0&&position==i%64,"recorded-note FIFO ordering/payload changed");
      }
      PlayHead playhead; audio.setPlayHead(&playhead);
      auto *data = processor->mData.getUISeqData();
      data->setRandomSeed(42);
      data->setMidiPassthru(SEQ_MIDI_PASSTHRU_ALL);
      data->setMappingCount(SEQMIDI_MAX_ITEMS);
      for (int index = 0; index < SEQMIDI_MAX_ITEMS; ++index) {
         auto *mapping = data->getMappingItem(index);
         mapping->mAction = index % 2 ? SEQMIDI_ACTION_CHGPAT : SEQMIDI_ACTION_PLAYBACK;
         mapping->mTarget = SEQMIDI_TARGET_ALL;
         mapping->mValue = index % 2 ? 1 : SEQMIDI_VALUE_PLAYBACK_START;
         mapping->mType = SEQ_MIDI_NOTEON; mapping->mNote = 127; mapping->mChannel = 1;
      }
      auto *layer = data->getLayer(0);
      layer->setMonoMode(false); layer->setMaxRows(129); layer->setMaxPoly(128);
      for (int row = 0; row < 128; ++row) {
         layer->setNote(row, (int8_t)row, false);
         layer->setProb(row, 0, 100, 0); layer->setVel(row, 0, 100, 0);
         layer->setLength(row,0,20,0);
      }
      processor->mData.swap();
      {
         juce::MemoryBlock saved, reloaded, refused;
         audio.getStateInformation(saved);
         auto restored = std::make_unique<SeqAudioProcessor>();
         auto &restoredAudio = static_cast<juce::AudioProcessor&>(*restored);
         require(restored->loadCanonicalState(saved.getData(), (int)saved.getSize()), "valid canonical state refused");
         restoredAudio.getStateInformation(reloaded);
         require(saved == reloaded, "canonical reload changed saved fields or long recorded cell lengths");
         const char malformed[]{'b','a','d'};
         require(!restored->loadCanonicalState(malformed, sizeof(malformed)), "malformed state accepted");
         auto invalid = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int)saved.getSize());
         require(invalid != nullptr, "fixture XML unavailable");
         auto *firstLayer = invalid->getChildByName("layer")->getChildByName("l");
         firstLayer->getChildByName("numsteps")->setAttribute("val", 999);
         juce::MemoryBlock badState;
         juce::AudioProcessor::copyXmlToBinary(*invalid, badState);
         require(!restored->loadCanonicalState(badState.getData(), (int)badState.getSize()), "invalid structural state accepted");
         restoredAudio.getStateInformation(refused);
         require(refused == saved, "refused state partially changed live sequence");
      }
      audio.setRateAndBufferSizeDetails(48000, 512); audio.prepareToPlay(48000, 512);
      juce::MidiBuffer midi; midi.ensureSize(SeqAudioProcessor::midiStorageBytes());
      // The adapter owns both swap buffers and prepares both before the callback.
      std::array<float, 512> left{}, right{}; float *channels[]{left.data(),right.data()};
      ResetListener listener;
      for (auto *parameter : audio.getParameters()) parameter->addListener(&listener);
      juce::MemoryBlock initialState; audio.getStateInformation(initialState);
      processor->mIncomingData.addToFifo(SEQ_REFRESH_MAP_MSG, 0, 0);
      std::array<uint8_t,16384> sysex{}; sysex.front()=0xf0; sysex.back()=0xf7;
      bool sysexSeen = false;
      size_t onsets = 0;
      std::array<int,16*128> sounding{};
      for (int block = 0; block < 300; ++block) {
         midi.clear();
         const int frames = block % 3 == 0 ? 1 : 512;
         juce::AudioBuffer<float> buffer(channels, 2, frames);
         if (block == 11) midi.addEvent(juce::MidiMessage::noteOff(1,127),0);
         if (block == 10) midi.addEvent(juce::MidiMessage::noteOn(1,127,(uint8_t)100),0);
         if (block == 30) midi.addEvent(sysex.data(),(int)sysex.size(),0);
         if (block == 5) require(processor->mIncomingData.addToFifo(SEQ_NOTIFY_HOST,0,0),"dirty command queue full");
         if (block == 180 || block == 251) playhead.frame = 0;
         if (block == 250) processor->mIncomingData.addToFifo(SEQ_MIDI_NOTEON,0,0);
         if (block == 200 || block == 299) playhead.playing = false;
         if (block == 220) { playhead.playing = true; playhead.frame = 0; }
         stochas_audit_begin();
         if (block == 100 || block == 120)
            for (auto *parameter : audio.getParameters()) parameter->setValue(block == 100 ? .25f : 0.0f);
         if (block == 250) {
            auto *swing = audio.getParameters()[52]; swing->setValue(.75f);
            std::array<float,55> values{};
            for (int index=0;index<55;++index) values[index]=audio.getParameters()[index]->getValue();
            audio.reset();
            for (int index=0;index<55;++index) require(audio.getParameters()[index]->getValue()==values[index], "CLAP reset changed parameters");
         }
         audio.processBlock(buffer, midi);
         stochas_audit_end(&a, &r, &l);
         if (a || r || l) {
            std::cerr << "RT violation block=" << block << " allocations=" << a << " releases=" << r << " locks=" << l << '\n';
            return 1;
         }
         // All native feedback must still arrive, with gestures, on control.
         processor->flushParameterResetNotifications();
         if (block == 200) {
            require(listener.values == 54 && listener.gestures == 108, "stop host feedback lost or duplicated");
            for (int index = 0; index < 54; ++index) require(audio.getParameters()[index]->getValue() == 0, "stop failed to reset automation");
            juce::MemoryBlock stoppedState; audio.getStateInformation(stoppedState);
            require(stoppedState == initialState, "stop/automation corrupted canonical saved pattern");
         }
         require(!processor->hasSchedulingOverload(), "ordinary128-note pattern overloaded");
         for (const auto event : midi) {
            if (event.numBytes == (int)sysex.size()) {
               require(std::memcmp(event.data,sysex.data(),sysex.size()) == 0, "SysEx passthrough changed bytes");
               sysexSeen = true;
            } else {
               const auto message = event.getMessage();
               if (message.isNoteOn()) {
                  ++onsets; ++sounding[(message.getChannel()-1)*128+message.getNoteNumber()];
               } else if (message.isNoteOff()) {
                  auto &voices = sounding[(message.getChannel()-1)*128+message.getNoteNumber()];
                  require(voices > 0, "orphan note off in lifecycle"); --voices;
               }
            }
         }
         if (block == 200 || block == 250 || block == 299)
            for (const int voices : sounding) require(voices == 0, "stop left a sounding note");
         for (int sample = 0; sample < frames; ++sample) require(left[sample] == 0 && right[sample] == 0, "note effect produced audio");
         playhead.frame += frames;
      }
      require(sysexSeen, "long SysEx passthrough lost");
      require(onsets >= 128, "dense chord lost notes");
      require(stochas_oracle_never_frontmost(), "owned oracle unexpectedly frontmost");
      audio.releaseResources();
      for (auto *parameter : audio.getParameters()) parameter->removeListener(&listener);
      std::cout << "PASS direct original-processor RT:128-note density,1/512frame splits,stop/host-reset/restart,retained parameters,no stale UI note,no allocation/free/mutex,zero audio\n";
   } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
