#ifndef EDITOR_SETUP_H
#define EDITOR_SETUP_H
#include "deck.h"
#include "../ocgcore/editor.h"
#include <string>
namespace ygo {
struct EditorSetup {
 DeckArray decks[2];uint32_t hand{0},hand1{0xffffffffU};
 bool Initialize(intptr_t h) const {return editor_setup_decks(h,decks[0].main.data(),decks[0].main.size(),decks[0].extra.data(),decks[0].extra.size(),decks[1].main.data(),decks[1].main.size(),decks[1].extra.data(),decks[1].extra.size(),hand,hand1)!=0;}
};
bool LoadEditorDeck(const wchar_t* filename,DeckArray& deck,std::wstring& error);
}
#endif
