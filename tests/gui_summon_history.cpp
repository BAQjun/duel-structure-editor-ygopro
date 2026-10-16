#include "../upstream/gframe/game.h"
#include "../upstream/gframe/duel_editor.h"
#include "../upstream/gframe/single_mode.h"
#include "../upstream/gframe/client_card.h"
#undef MAX_CARD_ID
#include "../upstream/gframe/data_manager.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
namespace ygo {
int stage=0,frames=0,last=0;bool success=false;size_t savedLogs=0;
irr::gui::IGUIElement* element(int id){auto* e=mainGame->env->getRootGUIElement()->getElementFromId(id,true);if(!e)throw std::runtime_error("Missing control "+std::to_string(id));return e;}
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);std::cout<<"PASS "<<text<<std::endl;}
void click(int id){irr::SEvent e{};e.EventType=irr::EET_GUI_EVENT;e.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;e.GUIEvent.Caller=element(id);mainGame->device->postEventFromUser(e);}
void text(int id,const wchar_t* value){static_cast<irr::gui::IGUIEditBox*>(element(id))->setText(value);irr::SEvent e{};e.EventType=irr::EET_GUI_EVENT;e.GUIEvent.EventType=irr::gui::EGET_EDITBOX_CHANGED;e.GUIEvent.Caller=element(id);mainGame->device->postEventFromUser(e);}
void select(int id,int row){auto* box=static_cast<irr::gui::IGUIComboBox*>(element(id));box->setSelected(row);irr::SEvent e{};e.EventType=irr::EET_GUI_EVENT;e.GUIEvent.EventType=irr::gui::EGET_COMBO_BOX_CHANGED;e.GUIEvent.Caller=box;mainGame->device->postEventFromUser(e);}
void choose(int id,uint32_t value){auto* box=static_cast<irr::gui::IGUIComboBox*>(element(id));for(unsigned i=0;i<box->getItemCount();++i)if(box->getItemData(i)==value){select(id,int(i));return;}throw std::runtime_error("Missing combo choice");}
bool contains(int id,uint32_t value){auto* box=static_cast<irr::gui::IGUIComboBox*>(element(id));for(unsigned i=0;i<box->getItemCount();++i)if(box->getItemData(i)==value)return true;return false;}
void row(int id,int n){static_cast<irr::gui::IGUIListBox*>(element(id))->setSelected(n);}
void next(int n){stage=n;last=frames;}
void search(uint32_t code){select(9015,0);text(9008,std::to_wstring(code).c_str());click(9001);choose(9006,LOCATION_MZONE);}
bool hasLog(const wchar_t* term){for(unsigned i=0;i<mainGame->lstLog->getItemCount();++i)if(std::wstring(mainGame->lstLog->getListItem(i)).find(term)!=std::wstring::npos)return true;return false;}
void shot(const char* path){auto* image=mainGame->driver->createScreenShot();if(image){mainGame->driver->writeImageToFile(image,path);image->drop();}}

void checked(int id,bool value){static_cast<irr::gui::IGUICheckBox*>(element(id))->setChecked(value);irr::SEvent e{};e.EventType=irr::EET_GUI_EVENT;e.GUIEvent.EventType=irr::gui::EGET_CHECKBOX_CHANGED;e.GUIEvent.Caller=element(id);mainGame->device->postEventFromUser(e);}
void chooseResult(){row(9905,0);irr::SEvent e{};e.EventType=irr::EET_GUI_EVENT;e.GUIEvent.EventType=irr::gui::EGET_LISTBOX_CHANGED;e.GUIEvent.Caller=element(9905);mainGame->device->postEventFromUser(e);}
void DuelEditorGuiTestTick(){try{++frames;if(frames>5000)throw std::runtime_error("GUI timeout");if(frames<last+18)return;
 if(stage==0){click(BUTTON_TEST_MODE);text(9207,L"0");text(9209,L"0");click(9201);next(1);return;}
 if(stage==1&&DuelEditor::active&&element(9003)->isEnabled()){search(37818794);choose(9771,LOCATION_EXTRA);next(2);return;}
 if(stage==2){require(element(9901)->isVisible(),"Fusion history option appears for Extra Deck special summon");checked(9901,true);click(9902);require(element(9900)->isVisible(),"Native YGO summon material window opens");text(9904,L"46986414");chooseResult();choose(9909,LOCATION_HAND);choose(9910,LOCATION_GRAVE);click(9915);text(9904,L"74677422");chooseResult();choose(9909,LOCATION_MZONE);text(9911,L"3");choose(9910,LOCATION_GRAVE);click(9915);require(static_cast<irr::gui::IGUIListBox*>(element(9916))->getItemCount()==2,"Native search configures two source-to-destination material rows");next(20);return;}
 if(stage==20){shot("../summon-materials-preview.png");click(9918);click(9003);next(3);return;}
 if(stage==3&&frames>last+240&&!mainGame->dField.mzone[0][0]){std::wcerr<<L"EDITOR STATUS "<<element(9020)->getText()<<std::endl;throw std::runtime_error("Summon generation failed");}
 if(stage==3&&mainGame->dField.mzone[0][0]){require(mainGame->dField.mzone[0][0]->code==37818794&&mainGame->dField.grave[0].size()==2,"Native GUI generates fusion host and both material instances at destinations");require(hasLog(L"召唤素材"),"Native history logs summon material movement");click(9007);next(4);return;}
 if(stage==4){require(!mainGame->dField.mzone[0][0]&&mainGame->dField.grave[0].empty(),"One native rewind removes host and material transaction");click(9402);next(5);return;}
 if(stage==5&&mainGame->dField.mzone[0][0]){require(DuelEditor::OpenCard(0,LOCATION_MZONE,0),"Existing fused monster opens for editing");require(static_cast<irr::gui::IGUICheckBox*>(element(9901))->isChecked(),"Existing monster restores corresponding Fusion summon choice");click(9902);require(static_cast<irr::gui::IGUIListBox*>(element(9916))->getItemCount()==2,"Existing fused monster restores both material movement rows");click(9918);success=true;SingleMode::StopPlay(true);next(6);return;}
 if(stage==6&&!DuelEditor::active){mainGame->device->closeDevice();return;}
 }catch(const std::exception& e){shot("../summon-gui-failure.png");std::cerr<<"FAIL "<<e.what()<<" stage="<<stage<<std::endl;SingleMode::StopPlay(true);mainGame->device->closeDevice();}}
}
int main(){using namespace ygo;std::setlocale(LC_CTYPE,".UTF-8");try{mainGame=new Game();require(mainGame->Initialize(),"Original YGOPro client initializes");ShowWindow(mainGame->hWnd,SW_HIDE);mainGame->MainLoop();if(success)std::cout<<"SUCCESS YGO summon history search materials source destination and rewind"<<std::endl;return success?0:1;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
