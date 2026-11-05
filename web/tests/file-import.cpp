// SPDX-License-Identifier: GPL-3.0-only
#include "FileImport.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void midi(const juce::File &path,int ppqn,double at) {
   juce::MidiMessageSequence sequence;auto note=juce::MidiMessage::noteOn(1,60,(uint8_t)100);
   note.setTimeStamp(at);sequence.addEvent(note);
   juce::MidiFile file;file.setTicksPerQuarterNote(ppqn);file.addTrack(sequence);
   juce::MemoryOutputStream bytes;
   require(file.writeTo(bytes),"MIDI fixture writer failed");
   require(path.replaceWithData(bytes.getData(),bytes.getDataSize()),"MIDI fixture save failed");
}
}
void stochas_test_file_import() {
   const auto directory=juce::File(std::getenv("STOCHAS_CONFIG_ROOT")).getChildFile("file-import-controls");
   require(directory.createDirectory().wasOk(),"owned import directory unavailable");
   juce::TemporaryFile notesFile(directory.getChildFile("notes.stnote"));
   const auto notes=notesFile.getFile();SeqFileImport::Notes rows;size_t count=0;juce::String error;
   require(notes.replaceWithText("60 \"\"snare\"\"\n-1 \"Muted\"\n127 \"Clap\\room\"\n"),"notes fixture save failed");
   require(SeqFileImport::notes(notes,rows,count,error)&&count==3&&rows[0].note==60&&rows[0].name=="\"snare\""&&
      rows[1].note==-1&&rows[2].note==127&&rows[2].name=="Clap\\room","original note format lost quotes/backslashes/off pitches");
   for(const auto *invalid:{"60", "128 \"x\"", "-2 \"x\"", "60 unquoted", "60 \"12345678901234567\"", "60 \"valid\"\n\n61 \"later\""}) {
      require(notes.replaceWithText(invalid),"invalid notes fixture save failed");count=999;
      require(!SeqFileImport::notes(notes,rows,count,error)&&count==999&&!error.isEmpty(),"malformed notes were accepted or committed count");
   }
   const unsigned char malformed[]{0xc0,0xaf};
   require(notes.replaceWithData(malformed,sizeof(malformed)),"UTF8 fixture save failed");
   require(!SeqFileImport::notes(notes,rows,count,error),"malformed UTF8 notes accepted");
   juce::TemporaryFile midiFile(directory.getChildFile("groove.mid"));const auto source=midiFile.getFile();
   for(int ppqn:{1,2,3,25,960}) {
      midi(source,ppqn,ppqn==960?300:1);
      std::array<int,SEQ_DEFAULT_NUM_STEPS> groove;groove.fill(7);
      require(SeqFileImport::groove(source,groove,error),"legal low/fractional PPQN groove was refused");
      const int step=ppqn==1?4:ppqn==2?2:ppqn==25?0:1;
      const int value=ppqn==3?33:ppqn==25?16:ppqn==960?25:0;
      for(size_t i=0;i<groove.size();++i)require(groove[i]==((int)i==step?value:7),"MIDI PPQN conversion changed another groove step");
   }
   juce::MemoryBlock bytes;require(source.loadFileAsData(bytes),"MIDI fixture read failed");
   for(bool smpte:{false,true}) {
      auto *raw=static_cast<uint8_t*>(bytes.getData());raw[12]=smpte?0xe7:0;raw[13]=smpte?40:0;
      require(source.replaceWithData(bytes.getData(),bytes.getSize()),"invalid MIDI fixture save failed");
      std::array<int,SEQ_DEFAULT_NUM_STEPS> groove;groove.fill(19);const auto before=groove;
      require(!SeqFileImport::groove(source,groove,error)&&groove==before,"invalid MIDI timing partially changed groove");
   }
   std::cout<<"PASS original file imports:quoted/off/UTF8 notes,transactional malformed rejection,PPQN1/2/3/25/960 and invalid timing\n";
}
