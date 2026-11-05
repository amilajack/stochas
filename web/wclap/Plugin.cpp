// SPDX-License-Identifier: GPL-3.0-only
#include "Plugin.h"
#include "Parameters.h"
#include "SequenceDelta.h"
#include "CompactState.h"
#include "EditorState.h"
#include <clap/ext/draft/webview.h>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
namespace Stochas::Wclap {
namespace {
constexpr uint32_t MaxFrames = 512, MaxEvents = 1024, MaxEventBytes = 32768;
class Position final : public juce::AudioPlayHead {
public:
   double beat = 0, tempo = 120, sampleRate = 48000;
   int64_t frame = 0;
   int numerator = 4, denominator = 4;
   bool playing = false, recording = false;
   juce::Optional<PositionInfo> getPosition() const override {
      PositionInfo value;
      value.setTimeInSamples(frame); value.setTimeInSeconds(frame / sampleRate);
      value.setPpqPosition(beat); value.setBpm(tempo);
      value.setTimeSignature(TimeSignature{numerator, denominator});
      value.setIsPlaying(playing); value.setIsRecording(recording);
      return value;
   }
   bool update(const clap_event_transport_t &value) noexcept {
      if (value.flags & CLAP_TRANSPORT_HAS_TEMPO) {
         if (!std::isfinite(value.tempo) || value.tempo < 20 || value.tempo > 999) return false;
         tempo = value.tempo;
      }
      if (value.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) beat = double(value.song_pos_beats) / CLAP_BEATTIME_FACTOR;
      if (value.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) {
         if (value.tsig_num < 1 || value.tsig_num > 99 ||
             !(value.tsig_denom == 1 || value.tsig_denom == 2 || value.tsig_denom == 4 ||
               value.tsig_denom == 8 || value.tsig_denom == 16 || value.tsig_denom == 32)) return false;
         numerator = value.tsig_num; denominator = value.tsig_denom;
      }
      playing = (value.flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
      recording = (value.flags & CLAP_TRANSPORT_IS_RECORDING) != 0;
      return std::isfinite(beat);
   }
   void advance(uint32_t frames) noexcept {
      frame += frames;
      if (playing) beat += double(frames) * tempo / (60.0 * sampleRate);
   }
};
struct Plugin {
   clap_plugin_t api{};
   const clap_host_t *host;
   std::unique_ptr<SeqAudioProcessor> processor;
   std::unique_ptr<Parameters> parameters;
   std::unique_ptr<SequenceData> compactDefaults;
   std::vector<uint8_t> compactBytes;
   std::vector<uint8_t> telemetryFrame,replyFrame,recordFrame,snapshotFrame,lastEditFrame;
   SeqPreparedFifo<1025> uiFeedback;
   bool uiFeedbackOverflow=false;
   Position position;
   juce::MidiBuffer midi;
   bool active = false, processing = false;
   uint32_t maximumFrames = 0;
   uint32_t recordNotifyFrames = 0;
#include "Webview.inc"
   explicit Plugin(const clap_host_t *h);
   juce::AudioProcessor &audio() { return *processor; }
   bool initialise() {
      processor = std::make_unique<SeqAudioProcessor>();
      compactDefaults=std::make_unique<SequenceData>();
      compactBytes.reserve(SeqCompactState::MaximumBytes);
      telemetryFrame.reserve(Protocol::maxTelemetryBytes);
      replyFrame.reserve(32);
      recordFrame.reserve(Protocol::maxTelemetryBytes);
      snapshotFrame.reserve(SeqCompactState::MaximumBytes+32);
      lastEditFrame.reserve(SeqCompactState::MaximumBytes+32);
      audio().setPlayHead(&position);
      parameters = std::make_unique<Parameters>(*processor);
      return parameters->count() == 55;
   }
   bool activate(double rate, uint32_t minimum, uint32_t maximum) {
      if (!processor || active || minimum < 1 || maximum < minimum || maximum > MaxFrames ||
          !(rate == 44100 || rate == 48000 || rate == 88200 || rate == 96000 || rate == 176400 || rate == 192000)) return false;
      position.sampleRate = rate; maximumFrames = maximum;
      midi.ensureSize(SeqAudioProcessor::midiStorageBytes());
      audio().setRateAndBufferSizeDetails(rate, (int)maximum);
      audio().prepareToPlay(rate, (int)maximum); active = true; return true;
   }
   bool applyParameter(const clap_event_header_t *event) noexcept {
      if (event->size != sizeof(clap_event_param_value_t)) return false;
      const auto &value = *reinterpret_cast<const clap_event_param_value_t*>(event);
      return parameters->set(value.param_id, value.value);
   }
   bool queueUiParameter(clap_id id,double value,int kind) {
      unsigned index=0;while(index<parameters->count()&&parameters->idAt(index)!=id)++index;
      if(index==parameters->count()||kind<0||kind>2)return false;
      if(kind==0&&!parameters->set(id,value))return false;
      const float normalised=parameters->find(id)->getValue();int32_t bits;std::memcpy(&bits,&normalised,4);
      if(!uiFeedback.addToFifo((kind<<8)|(int)index,bits,0))uiFeedbackOverflow=true;
      const auto *extension=static_cast<const clap_host_params_t*>(host->get_extension(host,CLAP_EXT_PARAMS));
      if(extension&&extension->request_flush)extension->request_flush(host);else host->request_process(host);
      return !uiFeedbackOverflow;
   }
   bool drainUiFeedback(const clap_output_events_t *output,uint32_t &count) {
      if(uiFeedbackOverflow||!output||!output->try_push)return false;
      int header,bits,unused;
      for(unsigned budget=0;budget<128&&uiFeedback.readFromFifo(&header,&bits,&unused);++budget) {
         if(++count>MaxEvents)return false;
         const auto id=parameters->idAt((unsigned)(header&255));const int kind=header>>8;
         if(kind==0) {
            float value;std::memcpy(&value,&bits,4);clap_event_param_value_t event{};
            event.header={sizeof(event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
            event.param_id=id;event.note_id=-1;event.port_index=event.channel=event.key=-1;event.value=value;
            if(!output->try_push(output,&event.header))return false;
         } else {
            clap_event_param_gesture_t event{};
            event.header={sizeof(event),0,CLAP_CORE_EVENT_SPACE_ID,uint16_t(kind==1?CLAP_EVENT_PARAM_GESTURE_BEGIN:CLAP_EVENT_PARAM_GESTURE_END),0};
            event.param_id=id;if(!output->try_push(output,&event.header))return false;
         }
      }
      if(uiFeedback.readyCount())host->request_process(host);
      return true;
   }
   bool addMidi(const clap_event_header_t *event, uint32_t start, uint32_t &bytes) {
      if (event->type == CLAP_EVENT_MIDI) {
         if (event->size != sizeof(clap_event_midi_t)) return false;
         const auto &value = *reinterpret_cast<const clap_event_midi_t*>(event);
         if (value.port_index != 0 || bytes > MaxEventBytes - 3) return false;
         bytes += 3;
         return midi.addEvent(value.data, 3, (int)(event->time - start));
      }
      if (event->type == CLAP_EVENT_MIDI_SYSEX) {
         if (event->size != sizeof(clap_event_midi_sysex_t)) return false;
         const auto &value = *reinterpret_cast<const clap_event_midi_sysex_t*>(event);
         if (value.port_index != 0 || !value.buffer || !value.size || value.size > MaxEventBytes - bytes) return false;
         bytes += value.size;
         return midi.addEvent(value.buffer, (int)value.size, (int)(event->time - start));
      }
      return true;
   }
   clap_process_status process(const clap_process_t *block) {
      if (!active || !processing || !block || !block->in_events || !block->out_events ||
          block->frames_count == 0 || block->frames_count > maximumFrames || block->audio_inputs_count || block->audio_outputs_count) return CLAP_PROCESS_ERROR;
      if (block->transport && !position.update(*block->transport)) return CLAP_PROCESS_ERROR;
      position.frame = block->steady_time;
      const uint32_t count = block->in_events->size(block->in_events);
      if (count > MaxEvents) return CLAP_PROCESS_ERROR;
      std::array<const clap_event_header_t*, MaxEvents> events{};
      uint32_t previousTime = 0;
      for (uint32_t index = 0; index < count; ++index) {
         events[index] = block->in_events->get(block->in_events, index);
         if (!events[index] || events[index]->size < sizeof(clap_event_header_t) || events[index]->time >= block->frames_count ||
             events[index]->time < previousTime) return CLAP_PROCESS_ERROR;
         previousTime = events[index]->time;
      }
      uint32_t cursor = 0, start = 0, outputCount = 0, inputBytes = 0;
      if(!drainUiFeedback(block->out_events,outputCount))return CLAP_PROCESS_ERROR;
      while (start < block->frames_count) {
         midi.clear();
         uint32_t end = block->frames_count;
         // Parameter/transport events split at their actual sample offset;
         // ordinary MIDI retains its timestamp in the current subspan.
         for (uint32_t index = cursor; index < count; ++index) {
            const auto *event = events[index];
            if (event->space_id == CLAP_CORE_EVENT_SPACE_ID &&
                (event->type == CLAP_EVENT_PARAM_VALUE || event->type == CLAP_EVENT_TRANSPORT) && event->time > start) {
               end = event->time; break;
            }
         }
         while (cursor < count && events[cursor]->time < end) {
            const auto *event = events[cursor++];
            if (event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            if (event->type == CLAP_EVENT_PARAM_VALUE) {
               if (!applyParameter(event)) return CLAP_PROCESS_ERROR;
            } else if (event->type == CLAP_EVENT_TRANSPORT) {
               if (event->size != sizeof(clap_event_transport_t) || !position.update(*reinterpret_cast<const clap_event_transport_t*>(event))) return CLAP_PROCESS_ERROR;
            } else if (!addMidi(event, start, inputBytes)) return CLAP_PROCESS_ERROR;
         }
         juce::AudioBuffer<float> silence(nullptr, 0, (int)(end - start));
         audio().processBlock(silence, midi);
         if(processor->mNotifier.hasCompletedMidiNotes()) {
            recordNotifyFrames+=(end-start);
            if(!position.playing||recordNotifyFrames>=(uint32_t)(position.sampleRate/30)) {
               requestMainThread();recordNotifyFrames=0;
            }
         } else recordNotifyFrames=0;
         if(processor->hasHostDirtyNotification())requestMainThread();
         if (processor->hasSchedulingOverload()) return CLAP_PROCESS_ERROR;
         const uint64_t resetFeedback=processor->consumeParameterResetNotifications();
         for(unsigned index=0;index<parameters->count();++index)if(resetFeedback&(uint64_t{1}<<index)) {
            clap_event_param_gesture_t begin{},finish{};
            begin.header={sizeof(begin),start,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_GESTURE_BEGIN,0};
            finish.header={sizeof(finish),start,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_GESTURE_END,0};
            begin.param_id=finish.param_id=parameters->idAt(index);
            clap_event_param_value_t value{};
            value.header={sizeof(value),start,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
            value.param_id=parameters->idAt(index);value.value=audio().getParameters()[(int)index]->getValue();
            value.note_id=-1;value.port_index=-1;value.channel=-1;value.key=-1;
            if(outputCount>MaxEvents-3||!block->out_events->try_push(block->out_events,&begin.header)||
               !block->out_events->try_push(block->out_events,&value.header)||!block->out_events->try_push(block->out_events,&finish.header))return CLAP_PROCESS_ERROR;
            outputCount+=3;
         }
         for (const auto event : midi) {
            if (++outputCount > MaxEvents) return CLAP_PROCESS_ERROR;
            const uint32_t time = start + (uint32_t)event.samplePosition;
            if (time >= block->frames_count) return CLAP_PROCESS_ERROR;
            if (event.numBytes <= 3) {
               clap_event_midi_t output{};
               output.header = {sizeof(output),time,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};
               std::memcpy(output.data,event.data,(size_t)event.numBytes);
               if (!block->out_events->try_push(block->out_events,&output.header)) return CLAP_PROCESS_ERROR;
            } else {
               clap_event_midi_sysex_t output{};
               output.header = {sizeof(output),time,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
               output.buffer=event.data; output.size=(uint32_t)event.numBytes;
               if (!block->out_events->try_push(block->out_events,&output.header)) return CLAP_PROCESS_ERROR;
            }
         }
         position.advance(end - start); start = end;
      }
      return !position.playing&&!count&&!outputCount?CLAP_PROCESS_SLEEP:CLAP_PROCESS_CONTINUE;
   }
   bool save(const clap_ostream_t *stream) {
      if (!stream || !stream->write) return false;
      applyRecordedNotes(true);
      SeqCompactState::encode(*processor->mData.getPublishedSeqData(),compactBytes);
      const auto *data=compactBytes.data();
      uint64_t remaining=compactBytes.size();
      while (remaining) {
         const int64_t written = stream->write(stream, data, remaining);
         if (written <= 0 || (uint64_t)written > remaining) return false;
         remaining -= (uint64_t)written; data += written;
      }
      return true;
   }
   bool load(const clap_istream_t *stream) {
      if (!stream || !stream->read || revision==UINT64_MAX) return false;
      compactBytes.resize(SeqCompactState::MaximumBytes);
      size_t size=0;
      while(size<compactBytes.size()) {
         const size_t available=compactBytes.size()-size;
         const int64_t count=stream->read(stream,compactBytes.data()+size,available);
         if(count<0||(uint64_t)count>available)return false;
         if (!count) break;
         size+=(size_t)count;
      }
      if(size>SeqCompactState::ProvenMaximumBytes)return false;
      compactBytes.resize(size);
      auto *candidate=processor->mData.getUISeqData();
      if(!SeqCompactState::decode(*candidate,*compactDefaults,compactBytes.data(),compactBytes.size())) {
         *candidate=*processor->mData.getPublishedSeqData();return false;
      }
      processor->commitPreparedState();
      ++revision;
      if(pageReady)sendSnapshot();
      return true;
   }
   const void *extension(const char *id);
};
Plugin *self(const clap_plugin_t *p) { return static_cast<Plugin*>(p->plugin_data); }
Plugin::Plugin(const clap_host_t *h) : host(h) {
   api={&descriptor,this,
      [](const clap_plugin_t *p){ return self(p)->initialise(); },
      [](const clap_plugin_t *p){ delete self(p); },
      [](const clap_plugin_t *p,double rate,uint32_t minimum,uint32_t maximum){ return self(p)->activate(rate,minimum,maximum); },
      [](const clap_plugin_t *p){ auto *s=self(p); s->processing=false; if(s->active)s->audio().releaseResources(); s->active=false; s->applyRecordedNotes(true); },
      [](const clap_plugin_t *p){ auto *s=self(p); return s->processing=s->active; },
      [](const clap_plugin_t *p){ auto *s=self(p);s->processing=false;if(s->processor->mNotifier.hasCompletedMidiNotes())s->requestMainThread(); },
      [](const clap_plugin_t *p){ self(p)->audio().reset(); },
      [](const clap_plugin_t *p,const clap_process_t *block){ return self(p)->process(block); },
      [](const clap_plugin_t *p,const char *id){ return self(p)->extension(id); },
      [](const clap_plugin_t *p){self(p)->onMainThread();}};
}
const clap_plugin_audio_ports_t audioPorts{
   [](const clap_plugin_t*,bool)->uint32_t{return 0;},
   [](const clap_plugin_t*,uint32_t,bool,clap_audio_port_info_t*){return false;}};
const clap_plugin_note_ports_t notePorts{
   [](const clap_plugin_t*,bool)->uint32_t{return 1;},
   [](const clap_plugin_t*,uint32_t index,bool input,clap_note_port_info_t *info){
      if(index || !info)return false; *info={}; info->id=input?32:4;
      info->supported_dialects=info->preferred_dialect=CLAP_NOTE_DIALECT_MIDI;
      std::strncpy(info->name,input?"Stochas MIDI input":"Stochas MIDI output",CLAP_NAME_SIZE-1);return true;}};
const clap_plugin_state_t state{
   [](const clap_plugin_t *p,const clap_ostream_t *stream){return self(p)->save(stream);},
   [](const clap_plugin_t *p,const clap_istream_t *stream){return self(p)->load(stream);}};
const clap_plugin_latency_t latency{[](const clap_plugin_t*)->uint32_t{return 0;}};
const clap_plugin_params_t params{
   [](const clap_plugin_t *p)->uint32_t{return self(p)->parameters->count();},
   [](const clap_plugin_t *p,uint32_t index,clap_param_info_t *info){return self(p)->parameters->info(index,info);},
   [](const clap_plugin_t *p,clap_id id,double *value){auto *parameter=self(p)->parameters->find(id);if(!parameter||!value)return false;*value=parameter->getValue();return true;},
   [](const clap_plugin_t *p,clap_id id,double value,char *text,uint32_t capacity){
      auto *parameter=self(p)->parameters->find(id);if(!parameter||!text||!capacity||!std::isfinite(value))return false;
      parameter->getText((float)value,(int)capacity-1).copyToUTF8(text,(int)capacity);return true;},
   [](const clap_plugin_t *p,clap_id id,const char *text,double *value){
      auto *parameter=self(p)->parameters->find(id);if(!parameter||!text||!value)return false;*value=parameter->getValueForText(text);return std::isfinite(*value);},
   [](const clap_plugin_t *p,const clap_input_events_t *input,const clap_output_events_t *output){
      if(!input||input->size(input)>MaxEvents)return;
      for(uint32_t i=0;i<input->size(input);++i){const auto *event=input->get(input,i);if(event&&event->space_id==CLAP_CORE_EVENT_SPACE_ID&&event->type==CLAP_EVENT_PARAM_VALUE)self(p)->applyParameter(event);}
      uint32_t count=0;if(!self(p)->drainUiFeedback(output,count))self(p)->uiFeedbackOverflow=true;}};
const clap_plugin_gui_t gui{
   [](const clap_plugin_t*,const char *api,bool floating){return api&&!std::strcmp(api,CLAP_WINDOW_API_WEBVIEW)&&!floating;},
   [](const clap_plugin_t*,const char **api,bool *floating){if(!api||!floating)return false;*api=CLAP_WINDOW_API_WEBVIEW;*floating=false;return true;},
   [](const clap_plugin_t *p,const char *api,bool floating){
      auto *s=self(p);if(!api||std::strcmp(api,CLAP_WINDOW_API_WEBVIEW)||floating||s->guiCreated)return false;
      s->guiCreated=true;s->pageReady=false;s->lastCommand=0;s->lastUiAcknowledged=0;s->lastEditFrame.clear();return true;},
   [](const clap_plugin_t *p){auto *s=self(p);s->guiCreated=false;s->guiVisible=false;s->uriRequested=false;s->pageReady=false;s->processor->requestAuditionRelease();s->host->request_process(s->host);},
   [](const clap_plugin_t *p,double value){if(!std::isfinite(value)||value<0.25||value>8)return false;self(p)->scale=value;if(self(p)->uriRequested)self(p)->sendScale();return true;},
   [](const clap_plugin_t *p,uint32_t *w,uint32_t *h){if(!w||!h)return false;*w=self(p)->width;*h=self(p)->height;return true;},
   [](const clap_plugin_t*){return false;},
   [](const clap_plugin_t*,clap_gui_resize_hints_t *hints){if(hints)*hints={};return false;},
   [](const clap_plugin_t *p,uint32_t *w,uint32_t *h){if(!w||!h)return false;*w=self(p)->width;*h=self(p)->height;return true;},
   [](const clap_plugin_t *p,uint32_t w,uint32_t h){return w==self(p)->width&&h==self(p)->height;},
   [](const clap_plugin_t*,const clap_window_t *window){return window&&window->api&&!std::strcmp(window->api,CLAP_WINDOW_API_WEBVIEW);},
   [](const clap_plugin_t*,const clap_window_t*){return false;},
   [](const clap_plugin_t*,const char*){},
   [](const clap_plugin_t *p){self(p)->guiVisible=true;return self(p)->guiCreated;},
   [](const clap_plugin_t *p){self(p)->guiVisible=false;return self(p)->guiCreated;}};
const clap_plugin_webview_t webview{
   [](const clap_plugin_t *p,char *uri,uint32_t capacity)->int32_t{
      constexpr char path[]="/ui/index.html";self(p)->uriRequested=true;
      if(uri&&capacity){std::strncpy(uri,path,capacity-1);uri[capacity-1]=0;}
      return sizeof(path);},
   [](const clap_plugin_t *p,const char *path,char *mime,uint32_t capacity,const clap_ostream_t *stream){return self(p)->getResource(path,mime,capacity,stream);},
   [](const clap_plugin_t *p,const void *data,uint32_t size){return self(p)->receive(data,size);}};
const void *Plugin::extension(const char *id) {
   if(!id)return nullptr;
   if(!std::strcmp(id,CLAP_EXT_AUDIO_PORTS))return &audioPorts;
   if(!std::strcmp(id,CLAP_EXT_NOTE_PORTS))return &notePorts;
   if(!std::strcmp(id,CLAP_EXT_LATENCY))return &latency;
   if(!std::strcmp(id,CLAP_EXT_PARAMS))return &params;
   if(!std::strcmp(id,CLAP_EXT_STATE))return &state;
   if(!std::strcmp(id,CLAP_EXT_GUI))return &gui;
   if(!std::strcmp(id,CLAP_EXT_WEBVIEW))return &webview;
   return nullptr;
}
}
const clap_plugin_t *createPlugin(const clap_host_t *host) {
   auto plugin=std::make_unique<Plugin>(host);return &plugin.release()->api;
}
}
