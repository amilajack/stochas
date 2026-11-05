// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "FileImport.h"
#include "Persist.h"
#include "PluginEditor.h"
#include "UiBridge.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <memory>
#include <pthread.h>
namespace {
std::unique_ptr<SeqAudioProcessor> processor;
std::unique_ptr<juce::AudioProcessorEditor> editor;
juce::String validationError;
}
extern "C" void stochas_dispatch_messages();
namespace {
void pumpEditor(void*){stochas_dispatch_messages();stochas_ui_pump();}
}
extern "C" int stochas_browser_font_generation;
extern "C" EMSCRIPTEN_KEEPALIVE void stochas_browser_fonts_added() {
    juce::Typeface::scanFolderForFonts(juce::File("/fonts/fallback"));
    juce::Typeface::clearTypefaceCache();
    ++stochas_browser_font_generation;
    if(editor)editor->repaint();
}
extern "C" EMSCRIPTEN_KEEPALIVE const char *stochas_browser_validate_file(const char *path) {
    validationError.clear();
    const auto name=juce::String::fromUTF8(path?path:"");
    if(!juce::File::isAbsolutePath(name))return "Invalid imported file path";
    const juce::File file(name);
    if(file.getSize()<=0||file.getSize()>SeqFileImport::maximumFileBytes)
        validationError="Unable to read file (maximum 64 MiB)";
    else if(file.hasFileExtension("stochas")) {
        auto xml=juce::XmlDocument::parse(file);
        if(!xml||!SeqPersist::validate(xml.get()))validationError="Invalid Stochas preset";
    } else if(file.hasFileExtension("stnote")) {
        SeqFileImport::Notes notes;size_t count=0;
        SeqFileImport::notes(file,notes,count,validationError);
    } else if(file.hasFileExtension("mid;midi")) {
        std::array<int,SEQ_DEFAULT_NUM_STEPS> groove{};
        SeqFileImport::groove(file,groove,validationError);
    } else validationError="Unsupported Stochas file type";
    return validationError.toRawUTF8();
}
int main() {
    setenv("JUCE_FONT_PATH","/fonts",1);setenv("HOME","/user",1);
    juce::File("/user").createDirectory();
    juce::initialiseJuce_GUI();
    processor = std::make_unique<SeqAudioProcessor>();
    editor.reset(static_cast<juce::AudioProcessor&>(*processor).createEditor());
    MAIN_THREAD_EM_ASM({StochasWclapPage.setWorker(PThread.pthreads[$0]);},pthread_self());
    editor->addToDesktop(0);
    editor->setVisible(true);
    stochas_ui_attach(*processor,*static_cast<SeqAudioProcessorEditor*>(editor.get()));
    // Control/state handoffs continue when a hidden page suspends RAF; paint
    // only on animation frames and use JUCE's actual dirty peer regions.
    pumpEditor(nullptr);emscripten_set_interval(pumpEditor,16,nullptr);
    emscripten_set_main_loop([] {
        for(int i=0;i<juce::ComponentPeer::getNumPeers();++i)
            juce::ComponentPeer::getPeer(i)->performAnyPendingRepaintsNow();
    },0,false);
    return 0;
}
