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
void DuelEditorGuiTestTick(){try{++frames;if(frames>5000)throw std::runtime_error("GUI timeout");if(frames<last+18)return;if(stage>=1&&stage<=5&&DuelEditor::active&&!SingleMode::AtBoundary())return;
 if(stage==0){click(BUTTON_TEST_MODE);text(9207,L"3");text(9209,L"7");click(9201);next(1);return;}
 if(stage==1&&DuelEditor::active&&element(9003)->isEnabled()){require(SingleMode::paused&&mainGame->dField.hand[0].empty()&&mainGame->dField.hand[1].empty(),"First real GUI point pauses before both opening hands");click(9402);next(2);return;}
 if(stage==2){if(mainGame->dField.hand[0].empty()){click(9402);last=frames;return;}require(mainGame->dField.hand[0].size()==3&&mainGame->dField.hand[1].empty()&&mainGame->dInfo.turn==0,"Native opening animation deals player one independently");require(hasLog(L"开局手牌"),"Opening deal writes native effect history");click(9007);next(3);return;}
 if(stage==3){require(mainGame->dField.hand[0].empty()&&!hasLog(L"开局手牌"),"Undo removes both dealt hand and later opening records");click(9402);next(4);return;}
 if(stage==4){require(mainGame->dField.hand[0].size()==3&&hasLog(L"开局手牌"),"Forward restores opening hand and history");click(9402);next(5);return;}
 if(stage==5){if(mainGame->dField.hand[1].size()!=7){click(9402);last=frames;return;}require(mainGame->dInfo.turn==0,"Opponent seven-card opening precedes turn one");for(uint32_t kind:{TYPE_FUSION,TYPE_SYNCHRO,TYPE_XYZ,TYPE_LINK}){uint32_t id=0;for(const auto& card:dataManager.GetDataTable())if(card.second.type&kind){id=card.first;break;}search(id);require(!contains(9771,LOCATION_HAND)&&!contains(9771,LOCATION_DECK)&&contains(9771,LOCATION_EXTRA),"Extra monster source dropdown excludes hand and deck");}search(84013237);click(9012);require(element(9870)->isVisible()&&element(9871)->isVisible(),"Xyz card shape contains both separate attachment actions");click(9871);require(std::wstring(element(9872)->getText())==L"添加超量素材","Material window contains only material editor");text(9880,L"89631139");text(9881,L"2");select(9882,1);row(9883,0);click(9886);require(static_cast<irr::gui::IGUIListBox*>(element(9884))->getItemCount()==2,"Card search configures two material copies before generation");click(9874);click(9870);require(std::wstring(element(9872)->getText())==L"放置指示物","Counter window is separate from materials");text(9880,L"魔力");text(9881,L"3");row(9883,0);click(9886);require(static_cast<irr::gui::IGUIListBox*>(element(9884))->getItemCount()==1,"Counter-name search configures desired quantity");next(51);return;}
 if(stage==51){shot("../v015-counter-preview.png");click(9874);click(9610);click(9003);next(6);return;}
 if(stage==6&&element(9003)->isEnabled()){auto* c=mainGame->dField.mzone[0][0];require(c&&c->overlayed.size()==2&&c->counters[1]==3,"Real native GUI shows generated Xyz materials and counter quantities");require(c->overlayed[0]->code==89631139,"Material card images and names receive real queried card codes");require(hasLog(L"超量素材")&&hasLog(L"指示物"),"Native log records editor material and counter modifications");click(9007);next(7);return;}
 if(stage==7){require(!mainGame->dField.mzone[0][0]&&!hasLog(L"超量素材"),"Single undo removes grouped generation and its effect records");click(9402);next(8);return;}
 if(stage==8){auto* c=mainGame->dField.mzone[0][0];require(c&&c->overlayed.size()==2&&c->counters[1]==3,"Forward restores actual material and counter display");require(DuelEditor::OpenCard(0,LOCATION_MZONE,0),"Existing Xyz opens for editing");click(9012);click(9871);require(static_cast<irr::gui::IGUIListBox*>(element(9884))->getItemCount()==2,"Existing material rows load native material identities");row(9884,0);click(9887);click(9874);click(9870);row(9884,0);text(9881,L"5");click(9888);click(9874);click(9610);click(9003);next(9);return;}
 if(stage==9&&element(9003)->isEnabled()){auto* c=mainGame->dField.mzone[0][0];require(c&&c->overlayed.size()==1&&c->counters[1]==5,"Existing save removes one material and updates counter quantity");savedLogs=mainGame->lstLog->getItemCount();SingleMode::QueueSave("../../work/gui-v015-history.yges","native history");next(10);return;}
 if(stage==10){text(9010,L"6500");click(9005);next(11);return;}
 if(stage==11&&mainGame->dInfo.lp[0]==6500){require(mainGame->lstLog->getItemCount()>savedLogs,"Later editor operation appends native history");SingleMode::QueueLoad("../../work/gui-v015-history.yges");next(12);return;}
 if(stage==12&&mainGame->dInfo.lp[0]==8000){auto* c=mainGame->dField.mzone[0][0];require(c&&c->overlayed.size()==1&&c->counters[1]==5&&mainGame->lstLog->getItemCount()==savedLogs,"Load restores saved counters materials and full history without later records");SingleMode::StopPlay(false);next(13);return;}
 if(stage==13&&mainGame->wReplaySave->isVisible()&&mainGame->fadingList.empty()){mainGame->ebRSName->setText(L"v015-native-gui");click(BUTTON_REPLAY_SAVE);next(14);return;}
 if(stage==14&&!DuelEditor::active&&!mainGame->dInfo.isStarted&&mainGame->fadingList.empty()){click(BUTTON_TEST_MODE);text(9207,L"0");text(9209,L"0");click(9201);next(15);return;}
 if(stage==15&&DuelEditor::active&&element(9003)->isEnabled()){require(!mainGame->dField.mzone[0][0],"Fresh editor session has no earlier field state");SingleMode::QueueLoad("../../work/gui-v015-history.yges");next(16);return;}
 if(stage==16&&mainGame->dField.mzone[0][0]){auto* c=mainGame->dField.mzone[0][0];require(c->overlayed.size()==1&&c->counters[1]==5&&mainGame->lstLog->getItemCount()==savedLogs,"New-session load restores archived effect history and attachments");element(9003)->getParent()->setVisible(false);mainGame->wInfos->setActiveTab(1);next(52);return;}
 if(stage==52){shot("../v015-history-preview.png");success=true;SingleMode::StopPlay(true);next(17);return;}
 if(stage==17&&!DuelEditor::active){mainGame->device->closeDevice();return;}
 }catch(const std::exception& e){shot("../v015-gui-failure.png");std::cerr<<"FAIL "<<e.what()<<" stage="<<stage<<" message="<<int(mainGame->dInfo.curMsg)<<std::endl;SingleMode::StopPlay(true);mainGame->device->closeDevice();}}
}
int main(){using namespace ygo;std::setlocale(LC_CTYPE,".UTF-8");try{mainGame=new Game();require(mainGame->Initialize(),"Original YGOPro client initializes");ShowWindow(mainGame->hWnd,SW_HIDE);mainGame->MainLoop();if(success)std::cout<<"SUCCESS YGO native opening materials counters source rules history rewind and cross-session load"<<std::endl;return success?0:1;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
