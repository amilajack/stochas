/***************************************************************
 ** Copyright (C) 2016 by Andrew Shakinovsky
 **
 ** You may also use this code under the terms of the 
 ** GPL v3 (see www.gnu.org/licenses).
 ** STOCHAS IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL 
 ** WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING 
 ** MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE DISCLAIMED.
 ***************************************************************/

#include "FileDialog.h"
void SeqFileDialog::endDialog(bool hitOk)
{   
   if (hitOk) {
#if JUCE_WASM
      if(mComputerResult.isNotEmpty()) {
         mFileName=mComputerResult;mComputerResult.clear();
         mLastLocation=File(mFileName).getParentDirectory();
         mGlob->mEditorState->setFileDirectory(mLastLocation.getFullPathName());
         notifyParent(0);return;
      }
#endif
      if (mBrowser->getNumSelectedFiles() > 0) {
         mLastLocation = mBrowser->getSelectedFile(0).getParentDirectory();
         mGlob->mEditorState->setFileDirectory(mLastLocation.getFullPathName());
         mFileName = mBrowser->getSelectedFile(0).getFullPathName();
         notifyParent(0);
      }
   }
}


void SeqFileDialog::notify(int id, int /*val*/) {
   switch (id) {
   case CPTID_OK:
      closeDialog(true);
      break;
   case CPTID_CANCEL:
      closeDialog(false);
      break;
#if JUCE_WASM
   case CPTID_COMPUTER:
      chooseComputerFile();
      break;
#endif
   default:
      jassertfalse;
   }
}

#define S_GAP     4 // gap between items
#define S_HEIGHT  22
#define S_BORDER  2  // size of border gap
#define S_TAB     80 // tab size (where labels are involved)
#define S_LISTWIDTH 50
void SeqFileDialog::resizedInner(Component * inner) {
   Rectangle<int> r = inner->getLocalBounds();
   Rectangle<int> btns, top;
   Rectangle<int> tmp;
   r.reduce(8, 8);
   btns = r.removeFromBottom(S_HEIGHT);
   top = r.removeFromTop(S_HEIGHT);
   mLblMain->setBounds(top);
   if(mBrowser)
      mBrowser->setBounds(r);

   // buttons
   tmp = btns.removeFromRight(60);
   mBtnCancel->setBounds(tmp);
   btns.removeFromRight(S_GAP);
   tmp = btns.removeFromRight(60);
   mBtnOk->setBounds(tmp);
#if JUCE_WASM
   mBtnComputer->setBounds(btns.removeFromLeft(190));
#endif

}

void SeqFileDialog::fileDoubleClicked(const File &)
{
   closeDialog(true);
}

void SeqFileDialog::doSetup(int mode)
{
   juce::Colour bg = mGlob->mEditorState->getColorFor(EditorState::background);
   juce::Colour fg = bg.contrasting(.5f);
   juce::Colour hl = mGlob->mEditorState->getColorFor(EditorState::coloredElements::toggleOff);

   bool saveMode = false;
   String txt;
   
   if (mBrowser) {
      removeFromInner(mBrowser.get());
      // TODO should we do the proper thing here with unique_ptr?
      delete mBrowser.release();
   }

   // this needs to stick around since the browser object sticks around and its referenced by that
   if (mFileFilter) {
      // TODO i switched this to unique_ptr, is there a better way?
      delete mFileFilter.release();
   }

   mMode = mode; // this is public and used by plugineditor to get results
   switch (mode) {
   case SEQ_FILE_SAVE_NOTES:
      txt = "Save note information to file";
      mFileFilter = std::unique_ptr<WildcardFileFilter>( new WildcardFileFilter("*.stnote", "", "note files"));
      saveMode = true;
      break;
   case SEQ_FILE_LOAD_NOTES:
      mFileFilter = std::unique_ptr<WildcardFileFilter>(new WildcardFileFilter("*.stnote", "", "note files"));
      txt = "Load note information from file";
      saveMode = false;
      break;
   case SEQ_FILE_LOAD_MIDI:
      mFileFilter = std::unique_ptr<WildcardFileFilter>(new WildcardFileFilter("*.*", "", "midi files"));
      txt = "Import groove from MIDI file";
      saveMode = false;
      break;
   case SEQ_FILE_SAVE_MIDI:
      mFileFilter = std::unique_ptr<WildcardFileFilter>(new WildcardFileFilter("*.*", "", "midi files"));
      txt = "Export groove to MIDI file";
      saveMode = true;
      break;
   case SEQ_FILE_LOAD_PATCH:
      mFileFilter = std::unique_ptr<WildcardFileFilter>(new WildcardFileFilter("*.stochas", "", "note files"));
      txt = "Load Stochas patch";
      saveMode = false;
      break;
   case SEQ_FILE_SAVE_PATCH:
      mFileFilter = std::unique_ptr<WildcardFileFilter>(new WildcardFileFilter("*.stochas", "", "note files"));
      txt = "Save Stochas patch";
      saveMode = true;
      break;
   default:
      jassertfalse;
   }


   int fflags = 0;
#if JUCE_WASM
   mComputerResult.clear();mSaveToComputer=saveMode;
   mBtnComputer->setButtonText(saveMode?"Download to computer...":"Choose on computer...");
#endif
   if (saveMode)
      fflags = FileBrowserComponent::FileChooserFlags::saveMode;
   else
      fflags = FileBrowserComponent::FileChooserFlags::openMode;
   fflags |= FileBrowserComponent::FileChooserFlags::canSelectFiles;

   mLblMain->setText(txt, juce::NotificationType::dontSendNotification);

   mBrowser = std::unique_ptr<FileBrowserComponent>(new FileBrowserComponent( fflags ,mLastLocation, mFileFilter.get(), 0)); 
   mBrowser->setLookAndFeel(&mLookAndFeel);
   mBrowser->getLookAndFeel().setColour(DirectoryContentsDisplayComponent::textColourId, fg);
   mBrowser->getLookAndFeel().setColour(DirectoryContentsDisplayComponent::highlightColourId, hl);
   fixColors(mGlob->mEditorState, mBrowser.get());
   addToInner(0, *mBrowser);
   mBrowser->setVisible(true);
   mBrowser->addListener(this);
   forceResize();
}

SeqFileDialog::SeqFileDialog(SeqGlob * glob, CptNotify * parent) :
   SeqModalDialog(glob, SEQCTL_FILEDIALOG, parent, 600, 500) 
{

   
   mBtnOk = std::unique_ptr<TextButton>(addStdButton("Ok", 0, CPTID_OK));
   mBtnCancel = std::unique_ptr<TextButton>(addStdButton("Cancel", 0, CPTID_CANCEL));
#if JUCE_WASM
   mBtnComputer=std::unique_ptr<TextButton>(addStdButton("Choose on computer...",0,CPTID_COMPUTER));
#endif

   mLblMain = std::unique_ptr<Label>(addStdLabel("Select a file")); // will be changed as appropriate
   mLblMain->setFont(Font(20.0f, Font::plain));
   mLblMain->setJustificationType(Justification::centred);
   // initial directory
   String dir = mGlob->mEditorState->getFileDirectory();
   if (dir.length())
      mLastLocation = File(dir);
   else
      mLastLocation = File::getSpecialLocation(File::userHomeDirectory);
}


SeqFileDialog::~SeqFileDialog() {
   // this avoids an assert related to look and feel (also probably a memory leak)
   // TODO is this still the case with new JUCE? also I switched this from ScopedPointer to unique_ptr
   if (mBrowser) {
      delete mBrowser.release();
   }
}

#if JUCE_WASM
void SeqFileDialog::chooseComputerFile()
{
   if (!mBtnComputer->isEnabled()) return;
   String pattern,defaultName;
   switch(mMode) {
   case SEQ_FILE_SAVE_NOTES:case SEQ_FILE_LOAD_NOTES:pattern="*.stnote";defaultName="Notes.stnote";break;
   case SEQ_FILE_SAVE_PATCH:case SEQ_FILE_LOAD_PATCH:pattern="*.stochas";defaultName="Untitled.stochas";break;
   default:pattern="*.mid;*.midi";defaultName="Groove.mid";break;
   }
   const File initial=mBrowser&&mBrowser->getNumSelectedFiles()?mBrowser->getSelectedFile(0):mLastLocation.getChildFile(defaultName);
   mComputerChooser=std::make_unique<FileChooser>(mSaveToComputer?"Save Stochas file":"Open Stochas file",initial,pattern,true);
   mBtnComputer->setEnabled(false);
   const int flags=FileBrowserComponent::canSelectFiles|(mSaveToComputer?FileBrowserComponent::saveMode:FileBrowserComponent::openMode);
   Component::SafePointer<SeqFileDialog> safe(this);
   mComputerChooser->launchAsync(flags,[safe](const FileChooser &chooser) {
      if(!safe)return;
      safe->mBtnComputer->setEnabled(true);
      if(chooser.getResult()==File{})return;
      safe->mComputerResult=chooser.getResult().getFullPathName();
      // Preserve the original synchronous save/load callback. The platform
      // writer copies the completed virtual file only after this returns.
      safe->closeDialog(true);
   });
}
#endif
