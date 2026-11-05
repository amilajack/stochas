// SPDX-License-Identifier: GPL-3.0-only
// The unchanged original editor owns its local model, files and preferences.
// Only typed model edits, commands and telemetry cross clap.webview/3. This
// processor never renders audio or runs a second sequencer in the editor.
#include "UiBridge.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "wclap/CompactState.h"
#include "wclap/SequenceDelta.h"
#include "wclap/Parameters.h"
#include <emscripten.h>
#include <array>
#include <memory>
namespace Protocol=Stochas::Wclap::Protocol;
namespace {
class Bridge final:public juce::AudioProcessorParameter::Listener {
   SeqAudioProcessor &processor;SeqAudioProcessorEditor &editor;
   Stochas::Wclap::Parameters parameters;
   std::unique_ptr<SequenceData> defaults=std::make_unique<SequenceData>();
   std::unique_ptr<SequenceData> remote=std::make_unique<SequenceData>();
   std::unique_ptr<SequenceData> sent=std::make_unique<SequenceData>();
   std::unique_ptr<SequenceData> incoming=std::make_unique<SequenceData>();
   std::unique_ptr<SequenceData> merged=std::make_unique<SequenceData>();
   bool applying=false,connected=false,waitingSnapshot=false,dirty=false,fullReplacement=false,sentFull=false;
   bool pendingModel=false,pendingCommand=false;
   uint64_t revision=0,sentExpected=0,sentNext=0,command=0,fullStateToken=0;
   double lastPoll=-1000,lastHello=-1000,lastModel=0,lastCommand=0;
   std::array<int,4> preferences{{-1,-1,-1,INT32_MIN}};
   std::vector<uint8_t> modelFrame,commandFrame,compact;
   void send(const std::vector<uint8_t> &bytes) {
      EM_ASM({const buffer=HEAPU8.slice($0,$0+$1).buffer;postMessage({stochasOut:buffer},[buffer]);},bytes.data(),bytes.size());
   }
   void hello() {
      Protocol::Writer frame(Protocol::Hello);frame.put(Protocol::version);send(frame.bytes);
      lastHello=emscripten_get_now();waitingSnapshot=true;
   }
   void changed() {
      if(applying)return;
      dirty=true;
      const auto token=processor.mData.getPublishedStateRevision();
      fullReplacement|=token!=fullStateToken;fullStateToken=token;
   }
   void publishUi(const SequenceData &value) {
      applying=true;*processor.mData.getUISeqData()=value;processor.mData.swap();
      fullStateToken=processor.mData.getPublishedStateRevision();
      processor.mNotifier.uiNeedsUpdate();editor.updateUI();applying=false;
   }
   void mergeRemote(bool acknowledged) {
      const bool replace=fullReplacement||(pendingModel&&sentFull&&!acknowledged);
      if(!connected)*merged=*incoming;
      else if(replace)*merged=*processor.mData.getPublishedSeqData();
      else SeqWireDelta::rebase(acknowledged?*sent:*remote,*processor.mData.getPublishedSeqData(),*incoming,*merged);
      *remote=*incoming;publishUi(*merged);dirty=true;
   }
   void sendModel(double now) {
      if(!dirty||pendingModel||waitingSnapshot||revision==UINT64_MAX)return;
      *sent=*processor.mData.getPublishedSeqData();sentExpected=revision;sentNext=revision+1;
      auto delta=SeqWireDelta::encode(*remote,*sent,revision);
      if(delta.bytes.empty()&&!delta.needsSnapshot&&!fullReplacement){dirty=false;return;}
      sentFull=fullReplacement;fullReplacement=false;
      if(sentFull||delta.needsSnapshot) {
         SeqCompactState::encode(*sent,compact);
         Protocol::Writer frame(Protocol::State);frame.put(revision).put(sentNext).append(compact.data(),compact.size());
         modelFrame=std::move(frame.bytes);++statesSent;
      } else {modelFrame=std::move(delta.bytes);++deltasSent;}
      pendingModel=true;dirty=false;lastModel=now;send(modelFrame);
   }
   void sendPreferences() {
      auto *state=processor.mEditorState;
      const std::array<int,4> current{{state->getCurrentLayer(),state->getDefaultProbability(true),
         state->getDefaultProbability(false),state->getPPQOffset()}};
      if(current==preferences)return;
      Protocol::Writer frame(Protocol::Preferences);
      frame.put(uint8_t(current[0])).put(int8_t(current[1])).put(int8_t(current[2])).put(int32_t(current[3]));
      send(frame.bytes);preferences=current;
      if(!state->saveSettings())editor.setAlertText("Unable to save editor preferences");
   }
   void sendCommands(double now) {
      if(processor.mIncomingData.consumeOverflow()) {
         processor.mIncomingData.discardPending();pendingCommand=false;
         Protocol::Writer release(Protocol::ReleaseAuditions);release.put(command);send(release.bytes);
         editor.setAlertText("Too many editor actions; audition notes were released");
      }
      if(pendingCommand||pendingModel||dirty||waitingSnapshot)return;
      Protocol::Writer frame(Protocol::Command);frame.put(command+1).put(uint32_t{0});
      int first,second,third;uint32_t count=0;
      while(count<Protocol::maxCommands&&processor.mIncomingData.readFromFifo(&first,&second,&third)) {
         frame.put(int32_t(first)).put(int32_t(second)).put(int32_t(third));++count;
      }
      if(!count)return;
      if(command==UINT64_MAX){editor.setAlertText("Editor action sequence exhausted; reopen the editor");return;}
      ++command;std::memcpy(frame.bytes.data()+12,&count,4);
      commandFrame=std::move(frame.bytes);pendingCommand=true;lastCommand=now;send(commandFrame);
   }
   bool telemetry(Protocol::Reader &frame) {
      uint64_t remoteRevision;uint8_t overload,recording,playing,update;int64_t seed;
      if(!frame.get(remoteRevision)||!frame.get(overload)||overload>1||!frame.get(recording)||recording>2||
         !frame.get(playing)||playing>2||!frame.get(update)||update>1||!frame.get(seed))return false;
      struct Layer {int32_t position,pattern;uint8_t muted;};std::array<Layer,SEQ_MAX_LAYERS> layers;
      for(auto &layer:layers)if(!frame.get(layer.position)||layer.position< -1||layer.position>=SEQ_MAX_STEPS||
         !frame.get(layer.pattern)||layer.pattern< -1||layer.pattern>=SEQ_MAX_PATTERNS||!frame.get(layer.muted)||layer.muted>1)return false;
      uint8_t hasMidi;int8_t type,channel,number,value;uint16_t recorded;
      if(!frame.get(hasMidi)||hasMidi>1||!frame.get(type)||!frame.get(channel)||!frame.get(number)||!frame.get(value)||
         !frame.get(recorded)||recorded)return false;
      if(hasMidi&&(type<SEQ_MIDI_NOTEON||type>SEQ_MIDI_CC||channel<1||channel>16||number<0||value<0))return false;
      constexpr int Words=(SEQ_MAX_ROWS+31)/32;
      std::array<uint32_t,SEQ_MAX_LAYERS*SEQ_MAX_STEPS*Words> words;
      for(auto &word:words)if(!frame.get(word))return false;
      std::array<double,55> values;
      for(auto &v:values)if(!frame.get(v)||!std::isfinite(v)||v<0||v>1)return false;
      if(frame.remaining())return false;
      auto &notify=processor.mNotifier;
      notify.setRandomSeed(seed);notify.setRecordingState((SeqProcessorNotifier::PlayRecordState)recording);
      notify.setPlaybackState((SeqProcessorNotifier::PlayRecordState)playing);
      for(int l=0;l<SEQ_MAX_LAYERS;++l) {
         notify.setPlayPosition(l,layers[l].position);notify.setCurrentPattern(l,layers[l].pattern);notify.setMuteState(l,layers[l].muted);
      }
      if(hasMidi)notify.setMidiEventOccurred(type,channel,number,value);
      size_t at=0;for(int l=0;l<SEQ_MAX_LAYERS;++l)for(int step=0;step<SEQ_MAX_STEPS;++step)
         for(int word=0;word<Words;++word)processor.setRemotePlayedWord(l,step,word,words[at++]);
      applying=true;for(unsigned i=0;i<values.size();++i)processor.getParameters()[(int)i]->setValue((float)values[i]);applying=false;
      if(update)notify.uiNeedsUpdate();
      if(overload)editor.setAlertText("Stochas MIDI scheduling capacity exceeded; stop playback and reduce event density");
      if(remoteRevision>revision&&!pendingModel&&!waitingSnapshot)hello();
      return true;
   }
public:
   unsigned deltasSent=0,statesSent=0;
   Bridge(SeqAudioProcessor &p,SeqAudioProcessorEditor &e):processor(p),editor(e),parameters(p) {
      compact.reserve(SeqCompactState::MaximumBytes);
      *remote=*processor.mData.getPublishedSeqData();fullStateToken=processor.mData.getPublishedStateRevision();
      processor.mData.setChangeNotify([](void *self){static_cast<Bridge*>(self)->changed();},this);
      for(auto *parameter:processor.getParameters())parameter->addListener(this);
   }
   void start(){MAIN_THREAD_EM_ASM({StochasWclapPage.ready();});hello();}
   bool isConnected()const{return connected;}
   void pump() {
      const double now=emscripten_get_now();
      if(!connected||waitingSnapshot){if(now-lastHello>1000)hello();return;}
      sendPreferences();sendModel(now);sendCommands(now);
      if(pendingModel&&now-lastModel>1000){lastModel=now;send(modelFrame);}
      if(pendingCommand&&now-lastCommand>1000){lastCommand=now;send(commandFrame);}
      if(now-lastPoll>=1000.0/30){Protocol::Writer poll(Protocol::Poll);send(poll.bytes);lastPoll=now;}
   }
   bool receive(const void *data,size_t size) {
      Protocol::Reader frame(data,size);if(!frame.valid())return false;
      if(frame.type()==Protocol::Snapshot) {
         uint32_t version;uint64_t current,ack;
         if(!frame.get(version)||version!=Protocol::version||!frame.get(current)||!frame.get(ack)||
            !SeqCompactState::decode(*incoming,*defaults,frame.rest(),frame.remaining()))return false;
         const bool accepted=pendingModel&&ack==sentNext;
         mergeRemote(accepted);revision=current;
         if(pendingModel&&sentFull&&!accepted)fullReplacement=true;
         pendingModel=false;sentFull=false;waitingSnapshot=false;connected=true;
         MAIN_THREAD_EM_ASM({StochasWclapPage.connected();});return true;
      }
      if(!connected)return false;
      if(frame.type()==Protocol::Telemetry)return telemetry(frame);
      if(frame.type()==Protocol::Delta) {
         uint64_t next;
         *incoming=*remote;
         if(!SeqWireDelta::applyPrepared(*incoming,*remote,data,size,revision,next)){hello();return true;}
         mergeRemote(false);revision=next;return true;
      }
      if(frame.type()==Protocol::Acknowledged) {
         uint64_t ack;if(!frame.get(ack)||frame.remaining())return false;
         if(pendingModel&&ack==sentNext) {
            if(revision!=sentExpected){hello();return true;}
            *remote=*sent;revision=ack;pendingModel=false;sentFull=false;
         }
         return true;
      }
      if(frame.type()==Protocol::NeedState) {uint64_t current;if(!frame.get(current)||frame.remaining())return false;hello();return true;}
      if(frame.type()==Protocol::CommandAcknowledged) {
         uint64_t ack;if(!frame.get(ack)||frame.remaining())return false;
         if(pendingCommand&&ack==command)pendingCommand=false;return true;
      }
      return false;
   }
   void parameterValueChanged(int index,float value) override {
      if(applying||!connected)return;
      Protocol::Writer frame(Protocol::Param);frame.put(parameters.idAt((unsigned)index)).put(double(value));send(frame.bytes);
   }
   void parameterGestureChanged(int index,bool begin) override {
      if(applying||!connected)return;
      Protocol::Writer frame(Protocol::Gesture);frame.put(parameters.idAt((unsigned)index)).put(uint8_t(begin));send(frame.bytes);
   }
};
std::unique_ptr<Bridge> bridge;
std::vector<uint8_t> receiveBuffer;
}
void stochas_ui_attach(SeqAudioProcessor &p,SeqAudioProcessorEditor &e){bridge=std::make_unique<Bridge>(p,e);bridge->start();}
void stochas_ui_pump(){if(bridge)bridge->pump();}
extern "C" EMSCRIPTEN_KEEPALIVE uint8_t *stochas_wclap_ui_buffer(int size) {
   if(size<=0||(size_t)size>Protocol::maxFrameBytes)return nullptr;
   receiveBuffer.resize((size_t)size);return receiveBuffer.data();
}
extern "C" EMSCRIPTEN_KEEPALIVE void stochas_wclap_ui_receive(const void *data,int size) {
   if(!bridge||size<=0||!bridge->receive(data,(size_t)size))
      EM_ASM({postMessage({stochasFailure:'Invalid plugin editor message; audio state was not changed'});});
}
extern "C" EMSCRIPTEN_KEEPALIVE int stochas_wclap_ui_connected(){return bridge&&bridge->isConnected();}
extern "C" EMSCRIPTEN_KEEPALIVE unsigned stochas_wclap_ui_deltas_sent(){return bridge?bridge->deltasSent:0;}
extern "C" EMSCRIPTEN_KEEPALIVE unsigned stochas_wclap_ui_states_sent(){return bridge?bridge->statesSent:0;}
