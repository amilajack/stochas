// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Original editor file operations. Parse completely on the UI before touching
// its sequence; these filesystem/format operations never run from process().
#include "SequenceData.h"
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
namespace SeqFileImport {
constexpr int64_t maximumFileBytes=64*1024*1024;
struct NoteRow { int8_t note=0; juce::String name; };
using Notes=std::array<NoteRow,SEQ_MAX_ROWS-1>;
inline bool notes(const juce::File &file,Notes &result,size_t &count,juce::String &error) {
   juce::MemoryBlock bytes;
   if(file.getSize()<=0||file.getSize()>maximumFileBytes||!file.loadFileAsData(bytes)) {
      error="Unable to read note list (maximum 64 MiB)";return false;
   }
   const auto *data=static_cast<const char*>(bytes.getData());
   if(std::memchr(data,0,bytes.getSize())||!juce::CharPointer_UTF8::isValidString(data,(int)bytes.getSize())) {
      error="Note list must contain valid UTF-8 text";return false;
   }
   auto lines=juce::StringArray::fromLines(juce::String::fromUTF8(data,(int)bytes.getSize()));
   while(!lines.isEmpty()&&lines[lines.size()-1].trim().isEmpty())lines.remove(lines.size()-1);
   if(lines.isEmpty()||lines.size()>(int)result.size()) {error="Note list must contain 1 to 128 rows";return false;}
   for(int i=0;i<lines.size();++i) {
      const auto line=lines[i].trim();
      const int split=line.indexOfAnyOf(" \t");
      if(split<=0) {error="Each note row needs a pitch and a quoted label";return false;}
      const auto pitch=line.substring(0,split);const char *first=pitch.toRawUTF8();
      const char *last=first+std::strlen(first);int value=0;
      const auto parsed=std::from_chars(first,last,value);
      const auto label=line.substring(split).trim();
      if(parsed.ec!=std::errc{}||parsed.ptr!=last||value< -1||value>127||label.length()<2||
         !label.startsWithChar('"')||!label.endsWithChar('"')) {
         error="Note pitch must be -1 to 127, followed by a quoted label";return false;
      }
      // Remove only the enclosing quotes. Quotes/backslashes inside labels
      // were written literally by the original format and must survive.
      const auto name=label.substring(1,label.length()-1);
      if(name.getNumBytesAsUTF8()>SEQ_MAX_NOTELABEL_LEN) {error="Note label exceeds 16 UTF-8 bytes";return false;}
      result[(size_t)i]={(int8_t)value,name};
   }
   count=(size_t)lines.size();return true;
}
inline bool groove(const juce::File &source,std::array<int,SEQ_DEFAULT_NUM_STEPS> &result,juce::String &error) {
   if(source.getSize()<=0||source.getSize()>maximumFileBytes) {error="Unable to read MIDI file (maximum 64 MiB)";return false;}
   auto stream=source.createInputStream();juce::MidiFile file;
   if(!stream||!file.readFrom(*stream)||file.getNumTracks()==0) {error="MIDI file has no valid tracks";return false;}
   if(file.getTimeFormat()<=0) {error="MIDI groove needs a positive ticks-per-quarter-note time format";return false;}
   const double ticksPerStep=file.getTimeFormat()/4.0;
   auto prepared=result;
   for(int track=0;track<file.getNumTracks();++track) {
      const auto *sequence=file.getTrack(track);
      for(int event=0;event<sequence->getNumEvents();++event) {
         if(!sequence->getEventPointer(event)->message.isNoteOn())continue;
         const double timestamp=sequence->getEventTime(event);
         if(!std::isfinite(timestamp)||timestamp<0) {error="MIDI groove contains an invalid timestamp";return false;}
         // Work within one measure, avoiding integer overflow on long files.
         const double position=std::fmod(timestamp/ticksPerStep,SEQ_DEFAULT_NUM_STEPS);
         int step=(int)std::floor(position);double distance=position-step;
         if(distance>0.5){++step;distance-=1;}
         prepared[(size_t)(step%SEQ_DEFAULT_NUM_STEPS)]=juce::roundToInt(distance*100.0);
      }
   }
   result=prepared;return true;
}
}
