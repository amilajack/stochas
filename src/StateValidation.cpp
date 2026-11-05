/***************************************************************
 ** Copyright (C) 2026 Stochas contributors
 ** GPL v3. See COPYING.
 ***************************************************************/
#include "Persist.h"
#include <array>
#include <bitset>
#include <charconv>
#include <cmath>
#include <limits>
namespace {
bool integer(const XmlElement &node,const char *attribute,int64_t minimum,int64_t maximum,bool optional=false,int64_t fallback=0) {
   if (!node.hasAttribute(attribute)) return optional && fallback>=minimum && fallback<=maximum;
   const String text=node.getStringAttribute(attribute);
   if (text.isEmpty() || text.getNumBytesAsUTF8()>32) return false;
   const char *first=text.toRawUTF8(), *last=first+text.getNumBytesAsUTF8();
   int64_t value=0; const auto parsed=std::from_chars(first,last,value);
   return parsed.ec==std::errc{} && parsed.ptr==last && value>=minimum && value<=maximum;
}
bool name(const XmlElement &node,size_t maximum) {return node.getStringAttribute("name").getNumBytesAsUTF8()<=maximum;}
bool number(const XmlElement &node,double minimum,double maximum) {
   const String text=node.getStringAttribute("val");
   if (text.isEmpty() || text.getNumBytesAsUTF8()>64) return false;
   const char *p=text.toRawUTF8(); if(*p=='-'||*p=='+')++p;
   bool digits=false; while(*p>='0'&&*p<='9'){digits=true;++p;}
   if(*p=='.'){++p;while(*p>='0'&&*p<='9'){digits=true;++p;}}
   if(!digits)return false;
   if(*p=='e'||*p=='E'){++p;if(*p=='-'||*p=='+')++p;bool exponent=false;while(*p>='0'&&*p<='9'){exponent=true;++p;}if(!exponent)return false;}
   if(*p)return false;
   const double value=text.getDoubleValue();return std::isfinite(value)&&value>=minimum&&value<=maximum;
}
bool scale(const XmlElement &node) {
   if(!integer(node,"stdoct",0,SEQ_NUM_OCTAVES-1))return false;
   const String key=node.getStringAttribute("stdkey"), selected=node.getStringAttribute("stdscale");
   bool keyFound=false,scaleFound=false;
   for(int i=0;i<SeqScale::getNumNoteNames();++i)if(key==SeqScale::getNoteName(i))keyFound=true;
   for(int i=0;i<SeqScale::getNumScales();++i)if(selected==SeqScale::getScaleName(i))scaleFound=true;
   return keyFound&&scaleFound;
}
bool pattern(const XmlElement &node) {
   if(!integer(node,"idx",0,SEQ_MAX_PATTERNS-1)||!name(node,SEQ_PATTERN_NAME_MAXLEN-1))return false;
   std::bitset<SEQ_MAX_ROWS> rows;
   std::array<uint16_t,SEQ_MAX_STEPS> chainCounts{};
   std::array<std::array<uint32_t,SEQ_MAX_CHAIN_SOURCES>,SEQ_MAX_STEPS> chainKeys{};
   for(auto *container:node.getChildIterator()) {
      if(!container->hasTagName("rows"))return false;
      for(auto *row:container->getChildIterator()) {
         if(!row->hasTagName("r")||!integer(*row,"idx",0,SEQ_MAX_ROWS-1))return false;
         const int r=row->getIntAttribute("idx");if(rows.test(r))return false;rows.set(r);
         std::bitset<SEQ_MAX_STEPS> cells;
         for(auto *cellContainer:row->getChildIterator()) {
            if(!cellContainer->hasTagName("cells"))return false;
            for(auto *cell:cellContainer->getChildIterator()) {
               if(!cell->hasTagName("c")||!integer(*cell,"idx",0,SEQ_MAX_STEPS-1)||
                  !integer(*cell,"prob",SEQ_PROB_OFF,SEQ_PROB_ON,true,SEQ_PROB_OFF)||
                  !integer(*cell,"velo",0,127,true)||!integer(*cell,"len",-(SEQ_MAX_RETRIGGER-1),127,true)||
                  !integer(*cell,"offs",-50,50,true))return false;
               const int column=cell->getIntAttribute("idx");if(cells.test(column))return false;cells.set(column);
               for(auto *chain:cell->getChildIterator()) {
                  if(!chain->hasTagName("cs")||!integer(*chain,"row",0,SEQ_MAX_ROWS-1)||
                     !integer(*chain,"col",0,SEQ_MAX_STEPS-1)||!integer(*chain,"neg",0,1,true)||
                     !integer(*chain,"negsrc",0,1,true))return false;
                  auto &count=chainCounts[column];if(count==SEQ_MAX_CHAIN_SOURCES)return false;
                  const uint32_t key=(uint32_t)(r*SEQ_MAX_ROWS+chain->getIntAttribute("row"))*SEQ_MAX_STEPS+chain->getIntAttribute("col");
                  for(uint16_t i=0;i<count;++i)if(chainKeys[column][i]==key)return false;
                  chainKeys[column][count++]=key;
               }
            }
         }
      }
   }
   return true;
}
bool layer(const XmlElement &node) {
   if(!integer(node,"idx",0,SEQ_MAX_LAYERS-1)||!name(node,SEQ_LAYER_NAME_MAXLEN-1))return false;
   std::bitset<SEQ_MAX_ROWS> notes;std::bitset<SEQ_MAX_PATTERNS> patterns;
   uint32_t seen=0;
   const auto once=[&](unsigned bit){const uint32_t mask=uint32_t{1}<<bit;if(seen&mask)return false;seen|=mask;return true;};
   for(auto *child:node.getChildIterator()) {
      if(child->hasTagName("notes")) {
         if(!once(0))return false;
         for(auto *note:child->getChildIterator()) {
            if(!note->hasTagName("n")||!integer(*note,"idx",0,SEQ_MAX_ROWS-1)||
               !integer(*note,"std",-1,127,true)||!integer(*note,"cust",-1,127,true)||!name(*note,SEQ_MAX_NOTELABEL_LEN))return false;
            const int index=note->getIntAttribute("idx");if(notes.test(index))return false;notes.set(index);
         }
      } else if(child->hasTagName("pats")) {
         if(!once(1))return false;
         for(auto *pat:child->getChildIterator()) {
            if(!pat->hasTagName("p")||!pattern(*pat))return false;
            const int index=pat->getIntAttribute("idx");if(patterns.test(index))return false;patterns.set(index);
         }
      } else if(child->hasTagName("sko")) {
         if(!once(2)||!scale(*child))return false;
      } else {
         struct Field {const char *tag;int lo,hi;};
         static constexpr Field fields[]{
            {"combine",0,1},{"humlen",0,100},{"humvel",0,100},{"humpos",0,50},{"mute",0,1},
            {"stppm",SEQ_MIN_STEPS,SEQ_DEFAULT_NUM_STEPS},{"dcycle",SEQ_DUTY_MIN,SEQ_DUTY_MAX},
            {"mchan",1,16},{"clkdiv",SEQ_MIN_CLOCK_DIV,SEQ_MAX_CLOCK_DIV},{"notecust",0,1},
            {"bias",SEQ_POLY_BIAS_MIN,SEQ_POLY_BIAS_MAX},{"maxpoly",1,SEQ_MAX_ROWS},{"mono",0,1},
            {"numsteps",SEQ_MIN_STEPS,SEQ_MAX_STEPS},{"numrows",SEQ_MIN_ROWS,SEQ_MAX_ROWS},{"curpat",0,SEQ_MAX_PATTERNS-1}};
         bool found=false;
         for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i)if(child->hasTagName(fields[i].tag)) {
            if(!once(i+3)||!integer(*child,"val",fields[i].lo,fields[i].hi)||child->getFirstChildElement())return false;
            found=true;break;
         }
         if(!found)return false;
      }
   }
   return true;
}
}
bool SeqPersist::validate(const XmlElement *root) {
   if(!root||!root->hasTagName("stochas")||!integer(*root,"version",SEQ_SERIALIZE_CURRENT_VERSION,SEQ_SERIALIZE_CURRENT_VERSION))return false;
   std::bitset<SEQ_MAX_LAYERS> layers;std::bitset<SEQMIDI_MAX_ITEMS> mappings;std::bitset<SEQ_DEFAULT_NUM_STEPS> groove;
   uint32_t seen=0;const auto once=[&](unsigned bit){const uint32_t mask=uint32_t{1}<<bit;if(seen&mask)return false;seen|=mask;return true;};
   for(auto *child:root->getChildIterator()) {
      if(child->hasTagName("layer")) {
         if(!once(0))return false;
         for(auto *item:child->getChildIterator()) {
            if(!item->hasTagName("l")||!layer(*item))return false;
            const int index=item->getIntAttribute("idx");if(layers.test(index))return false;layers.set(index);
         }
      } else if(child->hasTagName("midimap")) {
         if(!once(1))return false;
         for(auto *item:child->getChildIterator()) {
            if(!item->hasTagName("mm")||!integer(*item,"idx",0,SEQMIDI_MAX_ITEMS-1)||
               !integer(*item,"act",SEQMIDI_ACTION_INVALID,SEQMIDI_ACTION_TRANS_RELATIVE)||
               !integer(*item,"chn",0,16)||!integer(*item,"not",0,127)||!integer(*item,"tgt",0,SEQMIDI_TARGET_ALL)||
               !integer(*item,"typ",0,SEQ_MIDI_OTHER)||!integer(*item,"val",SEQMIDI_VALUE_SPD_HALF,127)||item->getFirstChildElement())return false;
            SeqMidiMapItem mapping((int8_t)item->getIntAttribute("act"),(int8_t)item->getIntAttribute("tgt"),
               (int8_t)item->getIntAttribute("val"),(int8_t)item->getIntAttribute("typ"),
               (int8_t)item->getIntAttribute("not"),(int8_t)item->getIntAttribute("chn"));
            if(!SeqPersist::validMapping(mapping))return false;
            const int index=item->getIntAttribute("idx");if(mappings.test(index))return false;mappings.set(index);
         }
      } else if(child->hasTagName("groove")) {
         if(!once(2))return false;
         for(auto *item:child->getChildIterator()) {
            if(!item->hasTagName("i")||!integer(*item,"idx",0,SEQ_DEFAULT_NUM_STEPS-1)||!integer(*item,"val",-50,50)||item->getFirstChildElement())return false;
            const int index=item->getIntAttribute("idx");if(groove.test(index))return false;groove.set(index);
         }
      } else if(child->hasTagName("seed")) {
         if(!once(3)||!integer(*child,"val",std::numeric_limits<int64_t>::min(),std::numeric_limits<int64_t>::max())||child->getFirstChildElement())return false;
      } else if(child->hasTagName("bpm")) {
         if(!once(4)||!number(*child,1,300)||child->getFirstChildElement())return false;
      } else {
         struct Field {const char *tag;int lo,hi;};
         static constexpr Field fields[]{{"autoplay",SEQ_PLAYMODE_AUTO,SEQ_PLAYMODE_MEASURE},{"offtime",SEQ_POS_OFFSET_MIN,SEQ_POS_OFFSET_MAX},
            {"swing",SEQ_MIN_SWING,SEQ_MAX_SWING},{"midipass",SEQ_MIDI_PASSTHRU_NONE,SEQ_MIDI_PASSTHRU_ALL},{"midiresp",SEQ_MIDI_RESPOND_NO,SEQ_MIDI_RESPOND_YES},{"curpat",0,SEQ_MAX_PATTERNS-1}};
         bool found=false;for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i)if(child->hasTagName(fields[i].tag)) {
            if(!once(i+5)||!integer(*child,"val",fields[i].lo,fields[i].hi)||child->getFirstChildElement())return false;found=true;break;
         }
         if(!found)return false;
      }
   }
   return true;
}

bool SeqPersist::validMapping(const SeqMidiMapItem &m) {
   if(m.mAction<SEQMIDI_ACTION_INVALID||m.mAction>SEQMIDI_ACTION_TRANS_RELATIVE||m.mTarget<0||
      m.mTarget>SEQMIDI_TARGET_ALL||m.mChannel<0||m.mChannel>16||m.mNote<0||m.mType<0||m.mType>SEQ_MIDI_CC)return false;
   // Preserve unfinished/disabled rows without turning them into active actions.
   if(m.mAction==SEQMIDI_ACTION_INVALID||m.mTarget==0||m.mChannel==0||m.mType==0||m.mValue==0)return true;
   switch(m.mAction) {
      case SEQMIDI_ACTION_CHGPAT:return m.mValue>=1&&m.mValue<=SEQMIDI_VALUE_PAT_PREV;
      case SEQMIDI_ACTION_MUTE:return m.mValue>=SEQMIDI_VALUE_MUTE_MUTE&&m.mValue<=SEQMIDI_VALUE_MUTE_TOGGLE;
      case SEQMIDI_ACTION_SPEED:return m.mValue==SEQMIDI_VALUE_SPD_HALF||m.mValue==SEQMIDI_VALUE_SPD_DBL||(m.mValue>=SEQ_MIN_CLOCK_DIV&&m.mValue<=SEQ_MAX_CLOCK_DIV);
      case SEQMIDI_ACTION_TRANS:return m.mValue>=SEQMIDI_VALUE_TRANSPOSE_MIN&&m.mValue<=SEQMIDI_VALUE_TRANS_PREV_LEGACY;
      case SEQMIDI_ACTION_TRANS_RELATIVE:return m.mValue>=SEQMIDI_VALUE_TRANSPOSE_MIN&&m.mValue<=SEQMIDI_VALUE_TRANSPOSE_MAX;
      case SEQMIDI_ACTION_STEPS:return m.mValue>=1&&m.mValue<=SEQ_MAX_STEPS;
      case SEQMIDI_ACTION_PBIAS:case SEQMIDI_ACTION_POSVAR:return m.mType==SEQ_MIDI_CC&&m.mValue==SEQMIDI_VALUE_VARIABLE;
      case SEQMIDI_ACTION_PLAYBACK:return m.mValue>=SEQMIDI_VALUE_PLAYBACK_START&&m.mValue<=SEQMIDI_VALUE_PLAYBACK_TOGGLE;
      case SEQMIDI_ACTION_RECORD:return m.mValue>=SEQMIDI_VALUE_RECORD_START&&m.mValue<=SEQMIDI_VALUE_RECORD_TOGGLE;
      case SEQMIDI_ACTION_RESET:return m.mValue>=SEQMIDI_ACTION_CHGPAT&&m.mValue<=SEQMIDI_ACTION_TRANS_RELATIVE&&m.mValue!=SEQMIDI_ACTION_RESET;
      default:return false;
   }
}
