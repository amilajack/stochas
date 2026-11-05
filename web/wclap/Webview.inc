// SPDX-License-Identifier: GPL-3.0-only
// Included inside the original-processor CLAP adapter. The host may service
// these calls on the DSP owner between blocks, or under its module mutex.
// "Main thread" therefore does not imply a separate non-realtime actor. The
// snapshot/recording work below remains under state-budget assessment; it must
// not be described as playback-safe before the actual host deadlines pass.
// There is no second app-host protocol or native-layout state.
uint64_t revision=0, lastCommand=0,lastUiAcknowledged=0;
bool uriRequested=false, pageReady=false, guiCreated=false, guiVisible=false;
uint32_t width=SEQ_SIZE_MAIN_W,height=SEQ_SIZE_MAIN_H;
double scale=0;
std::atomic<bool> mainRequested{false};
void requestMainThread() {
   if(!mainRequested.exchange(true,std::memory_order_acq_rel))host->request_callback(host);
}
void applyRecordedNotes(bool publishToPage) {
   if(!processor->mNotifier.hasCompletedMidiNotes())return;
   if(revision==UINT64_MAX)return;
   auto *data=processor->mData.getUISeqData();
   const int layerIndex=processor->mEditorState->getCurrentLayer();
   if(layerIndex<0||layerIndex>=SEQ_MAX_LAYERS)return;
   auto *layer=data->getLayer(layerIndex);
   const auto probability=processor->mEditorState->getDefaultProbability(layer->isMonoMode());
   Protocol::Writer update(Protocol::Delta,recordFrame);
   update.put(revision).put(revision+1).put(uint32_t{0});
   int number,velocity,length,position;uint32_t changed=0;
   for(unsigned budget=0;budget<1024&&processor->mNotifier.getCompletedMidiNote(&number,&velocity,&length,&position);++budget) {
      const int row=layer->getRowForNote((int8_t)number);
      if(row<0)continue;
      if(number<0||number>127||velocity<0||velocity>127||length<0||length>=SEQ_MAX_STEPS||position<0||position>=SEQ_MAX_STEPS)continue;
      layer->setProb(row,position,probability);layer->setVel(row,position,(int8_t)velocity);layer->setLength(row,position,(int8_t)length);
      ++changed;
      update.put(uint8_t(Protocol::SetCell)).put(uint8_t(layerIndex)).put(uint8_t(layer->getCurrentPattern()))
         .put(uint8_t(row)).put(uint8_t(position)).put(int8_t(probability)).put(int8_t(velocity))
         .put(int8_t(length)).put(int8_t(layer->getOffset(row,position)));
   }
   if(!changed)return;
   processor->mData.swap();markDirty();
   std::memcpy(update.bytes.data()+20,&changed,sizeof(changed));
   ++revision;
   if(publishToPage&&pageReady) {
      if(update.valid())sendFrame(update.bytes);
   }
}
void onMainThread() {
   mainRequested.store(false,std::memory_order_release);
   applyRecordedNotes(true);
   if(processor->consumeHostDirtyNotification())markDirty();
}
bool sendFrame(const std::vector<uint8_t> &bytes) {
   const auto *webview=static_cast<const clap_host_webview_t*>(host->get_extension(host,CLAP_EXT_WEBVIEW));
   return uriRequested&&webview&&webview->send(host,bytes.data(),(uint32_t)bytes.size());
}
void markDirty() {
   const auto *state=static_cast<const clap_host_state_t*>(host->get_extension(host,CLAP_EXT_STATE));
   if(state)state->mark_dirty(host);
}
bool sendRevision(Protocol::Type type,uint64_t value) {
   Protocol::Writer reply(type,replyFrame);reply.put(value);return reply.valid()&&sendFrame(reply.bytes);
}
bool sendSnapshot() {
   SeqCompactState::encode(*processor->mData.getPublishedSeqData(),compactBytes);
   Protocol::Writer reply(Protocol::Snapshot,snapshotFrame);
   reply.put(Protocol::version).put(revision).put(lastUiAcknowledged).append(compactBytes.data(),compactBytes.size());
   return reply.valid()&&sendFrame(reply.bytes);
}
bool sendScale() {Protocol::Writer out(Protocol::Scale,replyFrame);out.put(scale);return out.valid()&&sendFrame(out.bytes);}
bool sendTelemetry() {
   applyRecordedNotes(true);
   auto &notify=processor->mNotifier;
   Protocol::Writer out(Protocol::Telemetry,telemetryFrame);
   out.put(revision).put(uint8_t(processor->hasSchedulingOverload()||uiFeedbackOverflow)).put(uint8_t(notify.getRecordingState()))
      .put(uint8_t(notify.getPlaybackState())).put(uint8_t(notify.doesUINeedUpdate())).put(int64_t(notify.getRandomSeed()));
   for(int layer=0;layer<SEQ_MAX_LAYERS;++layer)
      out.put(int32_t(notify.getPlayPosition(layer))).put(int32_t(notify.getCurrentPattern(layer))).put(uint8_t(notify.getMuteState(layer)));
   int8_t type=0,channel=0,number=0,value=0;
   out.put(uint8_t(notify.getMidiEventOccurred(&type,&channel,&number,&value))).put(type).put(channel).put(number).put(value);
   // Recorded model edits are already committed by the plugin-main writer.
   // The original editor receives their semantic delta, not a lossy FIFO relay.
   out.put(uint16_t{0});
   for(int layer=0;layer<SEQ_MAX_LAYERS;++layer)
      for(int step=0;step<SEQ_MAX_STEPS;++step)
         for(int word=0;word<(SEQ_MAX_ROWS+31)/32;++word)out.put(processor->getPlayedWord(layer,step,word));
   for(auto *parameter:audio().getParameters())out.put(double(parameter->getValue()));
   if(!out.valid()||out.bytes.size()>Protocol::maxTelemetryBytes)return false;
   if(processor->consumeHostDirtyNotification())markDirty();
   return sendFrame(out.bytes);
}
bool receive(const void *data,uint32_t size) {
   Protocol::Reader in(data,size);
   if(!uriRequested||!in.valid())return false;
   switch(in.type()) {
   case Protocol::Hello: {
      uint32_t version;if(!in.get(version)||version!=Protocol::version||in.remaining())return false;
      applyRecordedNotes(false);pageReady=true;
      return sendSnapshot()&&sendScale()&&sendTelemetry();
   }
   case Protocol::Poll:return pageReady&&!in.remaining()&&sendTelemetry();
   case Protocol::ReleaseAuditions: {
      uint64_t cancelled;if(!in.get(cancelled)||in.remaining())return false;
      lastCommand=std::max(lastCommand,cancelled);processor->mIncomingData.discardPending();
      processor->requestAuditionRelease();host->request_process(host);return true;
   }
   case Protocol::Param: {
      uint32_t id;double value;
      return in.get(id)&&in.get(value)&&!in.remaining()&&queueUiParameter(id,value,0);
   }
   case Protocol::Gesture: {
      uint32_t id;uint8_t begin;
      return in.get(id)&&in.get(begin)&&begin<=1&&!in.remaining()&&queueUiParameter(id,0,begin?1:2);
   }
   case Protocol::Preferences: {
      uint8_t layer;int8_t mono,poly;int32_t ppq;
      if(!in.get(layer)||!in.get(mono)||!in.get(poly)||!in.get(ppq)||in.remaining()||
         layer>=SEQ_MAX_LAYERS||mono<SEQ_PROB_OFF||mono>SEQ_PROB_ON||poly<SEQ_PROB_OFF||poly>SEQ_PROB_ON||ppq<SEQ_POS_OFFSET_MIN||ppq>SEQ_POS_OFFSET_MAX)return false;
      auto *preferences=processor->mEditorState;
      preferences->setCurrentLayer(layer);preferences->setDefaultProbability(mono,true);
      preferences->setDefaultProbability(poly,false);preferences->setPPQOffset(ppq);return true;
   }
   case Protocol::Delta: {
      if(size==lastEditFrame.size()&&!std::memcmp(data,lastEditFrame.data(),size))
         return sendRevision(Protocol::Acknowledged,lastUiAcknowledged);
      if(size>lastEditFrame.capacity())return false;
      uint64_t next;
      if(!SeqWireDelta::applyPrepared(*processor->mData.getUISeqData(),*processor->mData.getPublishedSeqData(),data,size,revision,next))
         return sendRevision(Protocol::NeedState,revision);
      processor->mData.swap();revision=next;
      lastEditFrame.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size);
      lastUiAcknowledged=next;
      markDirty();host->request_process(host);
      return sendRevision(Protocol::Acknowledged,revision);
   }
   case Protocol::State: {
      if(size==lastEditFrame.size()&&!std::memcmp(data,lastEditFrame.data(),size))
         return sendRevision(Protocol::Acknowledged,lastUiAcknowledged);
      if(size>lastEditFrame.capacity())return false;
      uint64_t expected,next;
      if(!in.get(expected)||!in.get(next)||expected!=revision||revision==UINT64_MAX||next!=revision+1)
         return sendRevision(Protocol::NeedState,revision);
      auto *candidate=processor->mData.getUISeqData();
      if(!SeqCompactState::decode(*candidate,*compactDefaults,in.rest(),in.remaining())) {
         *candidate=*processor->mData.getPublishedSeqData();return false;
      }
      processor->commitPreparedState();
      revision=next;markDirty();host->request_process(host);
      lastEditFrame.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size);
      lastUiAcknowledged=next;
      return sendRevision(Protocol::Acknowledged,revision);
   }
   case Protocol::Command: {
      uint64_t sequence;uint32_t count;
      if(!in.get(sequence)||!in.get(count)||!count||count>FIFO_SIZE-1||in.remaining()!=size_t(count)*12)return false;
      if(sequence==lastCommand)return sendRevision(Protocol::CommandAcknowledged,lastCommand);
      if(lastCommand==UINT64_MAX||sequence!=lastCommand+1||processor->mIncomingData.freeSpace()<(int)count)return false;
      std::array<std::array<int32_t,3>,FIFO_SIZE-1> commands{};
      for(unsigned i=0;i<count;++i) {
         auto &command=commands[i];for(auto &value:command)if(!in.get(value))return false;
         switch(command[0]) {
         case SEQ_MIDI_NOTEON:case SEQ_MIDI_NOTEOFF:
            // Original NotePanel sends separate layer and note-row indices.
            if(command[1]<0||command[1]>=SEQ_MAX_LAYERS||command[2]<0||command[2]>=SEQ_MAX_ROWS)return false;
            break;
         case SEQ_REFRESH_MAP_MSG:case SEQ_NOTIFY_HOST:case SEQ_SET_RECORD_MODE:
         case SEQ_SET_PLAY_START_STOP:case SEQ_STANDALONE_SET_TEMPO:
            if(command[1]||command[2])return false;break;
         default:return false;
         }
      }
      for(unsigned i=0;i<count;++i)
         if(!processor->mIncomingData.addToFifo(commands[i][0],commands[i][1],commands[i][2]))return false;
      lastCommand=sequence;host->request_process(host);
      return sendRevision(Protocol::CommandAcknowledged,lastCommand);
   }
   case Protocol::Resize: {
      uint32_t w,h;if(!in.get(w)||!in.get(h)||in.remaining()||w<64||h<64||w>16384||h>16384)return false;
      width=w;height=h;
      const auto *gui=static_cast<const clap_host_gui_t*>(host->get_extension(host,CLAP_EXT_GUI));
      if(gui&&guiCreated)gui->request_resize(host,w,h);return true;
   }
   default:return false;
   }
}
bool getResource(const char *path,char *mime,uint32_t capacity,const clap_ostream_t *stream) {
   if(!uriRequested||!path||!mime||!capacity||!stream||!stream->write)return false;
   const std::string relative(path);
   if(relative.compare(0,4,"/ui/")||relative.size()>4096||relative.find('\\')!=std::string::npos)return false;
   size_t start=4;
   while(start<=relative.size()) {
      const auto end=relative.find('/',start);
      const auto part=relative.substr(start,end==std::string::npos?end:end-start);
      if(part.empty()||part=="."||part=="..")return false;
      if(end==std::string::npos)break;
      start=end+1;
   }
   const auto extension=relative.substr(relative.find_last_of('.')+1);
   const char *kind=extension=="html"?"text/html":extension=="js"?"application/javascript":
      extension=="wasm"?"application/wasm":extension=="otf"?"font/otf":extension=="ttf"?"font/ttf":"application/octet-stream";
   if(std::strlen(kind)>=capacity)return false;
   std::strcpy(mime,kind);
   juce::FileInputStream input(juce::File(bundlePath+relative));
   if(!input.openedOk()||input.getTotalLength()<0||input.getTotalLength()>int64_t(Protocol::maxFrameBytes))return false;
   std::array<uint8_t,16384> buffer{};
   while(!input.isExhausted()) {
      const int count=input.read(buffer.data(),buffer.size());if(count<=0)return false;
      int offset=0;while(offset<count) {
         const int64_t written=stream->write(stream,buffer.data()+offset,(uint64_t)(count-offset));
         if(written<=0||written>count-offset)return false;offset+=(int)written;
      }
   }
   return true;
}
