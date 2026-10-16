#include "../upstream/gframe/game.h"
#include "../upstream/gframe/duel_editor.h"
#include "../upstream/gframe/duelclient.h"
#include "../upstream/gframe/client_card.h"
#include "../upstream/gframe/single_mode.h"
#include "../upstream/gframe/replay_mode.h"
#include "../upstream/gframe/image_manager.h"
#include "../upstream/gframe/data_manager.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
namespace ygo {
static int stage=0,frames=0,last=0;static bool success=false;
irr::gui::IGUIElement* element(int id) {
 auto* item=mainGame->env->getRootGUIElement()->getElementFromId(id,true);
 if(!item)throw std::runtime_error("Missing GUI element "+std::to_string(id));return item;
}
void click(int id) {
 irr::SEvent event{};event.EventType=irr::EET_GUI_EVENT;event.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;event.GUIEvent.Caller=element(id);
 mainGame->device->postEventFromUser(event);
}
void require(bool state,const char* text){if(!state)throw std::runtime_error(text);std::cout<<"PASS "<<text<<std::endl;}
void DuelEditorGuiTestTick() {
 try {
  ++frames;
  if(frames>2400)throw std::runtime_error("GUI timed out in stage "+std::to_string(stage));
  const bool ready=DuelEditor::active && mainGame->dInfo.curMsg==MSG_SELECT_IDLECMD && element(9000)->isEnabled();
  if(stage==0 && frames>10){
   auto* picture=imageManager.GetImage(549481);require(picture!=nullptr,"External card library loads image absent from bundled samples");
   require(picture->getDimension().Width>100 && picture->getDimension().Height>100,"External card art decodes into a usable image");picture->drop();
   click(BUTTON_TEST_MODE);require(element(9200)->isVisible(),"Editor entry opens both-player deck selection");click(9201);if(!DuelEditor::active){char detail[1024]{};BufferIO::EncodeUTF8(static_cast<irr::gui::IGUIStaticText*>(element(9208))->getText(),detail);throw std::runtime_error(detail);}stage=1;last=frames;return;
  }
  if(stage==1 && ready) {
   require(mainGame->dInfo.isStarted,"Main-menu editor entry starts a real local duel");
   click(9300);auto* list=static_cast<irr::gui::IGUIListBox*>(element(9304));require(list->getItemCount()==40,"Order viewer lists every selected main-deck card");list->setSelected(0);click(9310);stage=30;last=frames;return;
  }
  if(stage==30 && ready && frames>last+3) {
   auto* list=static_cast<irr::gui::IGUIListBox*>(element(9304));require(list->getSelected()==39,"Actual GUI sort moves selected card from top to bottom");click(9311);click(9000);click(9007);stage=31;last=frames;return;
  }
  if(stage==31 && ready && frames>last+3) {
   click(9004);click(9300);auto* list=static_cast<irr::gui::IGUIListBox*>(element(9304));require(list->getSelected()==0,"GUI undo restores original deck order and selected identity");click(9311);
   click(9000);static_cast<irr::gui::IGUIEditBox*>(element(9008))->setText(L"55144522");click(9001);click(9003);stage=2;last=frames;return;
  }
  if(stage==2 && ready && mainGame->dField.hand[0].size()==1) {
   require(mainGame->dField.hand[0][0]->code==55144522,"GUI creation refreshes actual hand card");
   require(mainGame->dField.activatable_cards.size()==1,"GUI gets regenerated legal activation");
   click(9004);DuelClient::SetResponseI(5);DuelClient::SendResponse();stage=3;last=frames;return;
  }
  if(stage==3 && frames-last>30) {
   if(ready && mainGame->dField.hand[0].size()==2 && mainGame->dField.grave[0].size()==1) {
    require(true,"GUI continues into real Pot of Greed effect resolution");
    click(9000);require(element(9007)->isEnabled(),"Undo button enabled after completed game action");click(9007);stage=4;last=frames;return;
   }
   if(mainGame->dInfo.curMsg==MSG_SELECT_CHAIN) {DuelClient::SetResponseI(-1);DuelClient::SendResponse();last=frames;}
   else if(mainGame->dInfo.curMsg==MSG_SELECT_PLACE) {
    unsigned char resp[3]{};auto allowed=mainGame->dField.selectable_field;bool found=false;
    for(int p=0;p<2&&!found;++p)for(int z=0;z<2&&!found;++z)for(int i=0;i<(z?8:7);++i)if(allowed&(1u<<(p*16+z*8+i))) {
     resp[0]=uint8_t(p);resp[1]=z?LOCATION_SZONE:LOCATION_MZONE;resp[2]=uint8_t(i);found=true;break;
    }
    if(found){DuelClient::SetResponseB(resp,3);DuelClient::SendResponse();last=frames;}
   }
  }
  if(stage==4 && ready && mainGame->dField.hand[0].size()==1 && mainGame->dField.grave[0].empty()) {
   require(mainGame->dField.hand[0][0]->code==55144522,"GUI undo restores spell and reverses its draw effect");click(9007);stage=5;last=frames;return;
  }
  if(stage==5 && ready && mainGame->dField.hand[0].empty()) {
   require(!element(9007)->isEnabled(),"Initial checkpoint disables undo");
   static_cast<irr::gui::IGUIComboBox*>(element(9011))->setSelected(1);click(9003);stage=6;last=frames;return;
  }
  if(stage==6 && ready && mainGame->dField.hand[1].size()==1) {
   require(mainGame->dField.hand[1][0]->code==55144522,"GUI can generate cards for opposing player");
   static_cast<irr::gui::IGUIEditBox*>(element(9010))->setText(L"6500");click(9005);stage=7;last=frames;return;
  }
  if(stage==7 && ready && mainGame->dInfo.lp[1]==6500) {
   require(true,"GUI LP edit applies to opposing player");click(9007);stage=8;last=frames;return;
  }
  if(stage==8 && ready && mainGame->dInfo.lp[1]==8000) {
   require(true,"GUI undo restores LP");
   auto* image=mainGame->driver->createScreenShot();
   if(image){require(mainGame->driver->writeImageToFile(image,"../editor-preview.png"),"Actual rendered GUI screenshot saves");image->drop();}
   click(9004);click(BUTTON_LEAVE_GAME);stage=9;last=frames;return;
  }
  if(stage==9 && !DuelEditor::active && !mainGame->dInfo.isStarted && mainGame->wMainMenu->isVisible()) {
   require(true,"Editor session exits and saves replay without deadlock");click(BUTTON_TEST_MODE);require(element(9200)->isVisible(),"Editor entry opens both-player deck selection");click(9201);if(!DuelEditor::active){char detail[1024]{};BufferIO::EncodeUTF8(static_cast<irr::gui::IGUIStaticText*>(element(9208))->getText(),detail);throw std::runtime_error(detail);}stage=10;last=frames;return;
  }
  if(stage==10 && ready) {
   require(mainGame->dField.hand[0].empty()&&mainGame->dField.hand[1].empty(),"Second editor session starts with fresh state");
   click(9000);require(!element(9007)->isEnabled(),"Second session has no undo history from prior session");
   auto* region=static_cast<irr::gui::IGUIComboBox*>(element(9006));region->setSelected(1);
   irr::SEvent changed{};changed.EventType=irr::EET_GUI_EVENT;changed.GUIEvent.EventType=irr::gui::EGET_COMBO_BOX_CHANGED;changed.GUIEvent.Caller=region;mainGame->device->postEventFromUser(changed);
   auto* search=static_cast<irr::gui::IGUIEditBox*>(element(9008));
   for(uint32_t type:{TYPE_RITUAL,TYPE_FUSION,TYPE_SYNCHRO,TYPE_XYZ,TYPE_LINK,TYPE_SPSUMMON,TYPE_TOKEN}) {
    uint32_t code=0;for(const auto& row:dataManager.GetDataTable())if((row.second.type&TYPE_MONSTER)&&(row.second.type&type)){code=row.first;break;}
    if(!code)continue;search->setText(std::to_wstring(code).c_str());click(9001);
    auto* summon=static_cast<irr::gui::IGUIComboBox*>(element(9013));
    bool hasNormal=false;for(unsigned int i=0;i<summon->getItemCount();++i)hasNormal|=summon->getItemData(i)==EDITOR_SUMMON_NORMAL;
    require(!hasNormal,"GUI hides normal summon for ineligible monster category");
   }
   search->setText(L"89631139");click(9001);
   auto* summon=static_cast<irr::gui::IGUIComboBox*>(element(9013));bool hasNormal=false;
   for(unsigned int i=0;i<summon->getItemCount();++i)hasNormal|=summon->getItemData(i)==EDITOR_SUMMON_NORMAL;
   require(hasNormal,"Tribute monster retains normal-summon history choice");
   search->setText(L"549481");click(9001);
   for(unsigned int i=0;i<summon->getItemCount();++i)if(summon->getItemData(i)==EDITOR_SUMMON_NORMAL)summon->setSelected(i);
   static_cast<irr::gui::IGUIComboBox*>(element(9011))->setSelected(0);static_cast<irr::gui::IGUICheckBox*>(element(9018))->setChecked(true);click(9003);stage=13;last=frames;return;
  }
  if(stage==13 && ready && mainGame->dField.mzone[0][0]) {
   require(mainGame->dField.mzone[0][0]->code==549481,"GUI generates monster using external card image source");
   require(!(mainGame->dField.mzone[0][0]->cmdFlag&COMMAND_REPOS),"GUI does not offer forbidden position change after current-turn summon");
   require(mainGame->dField.mzone[0][0]->owner==1 && mainGame->dField.mzone[0][0]->controler==0,"Opponent-owned generated card remains on chosen side in GUI");
   require(DuelEditor::OpenCard(0,LOCATION_MZONE,0),"Every field card can open its existing-card editor");
   require(element(9017)->isVisible() && element(9017)->isEnabled(),"Selected card editor exposes deletion action");
   auto* summon=static_cast<irr::gui::IGUIComboBox*>(element(9013));summon->setSelected(0);
   static_cast<irr::gui::IGUIComboBox*>(element(9019))->setSelected(1);click(9003);stage=15;last=frames;return;
  }
  if(stage==15 && ready && mainGame->dField.mzone[0][0] && mainGame->dField.mzone[0][0]->position==POS_FACEUP_DEFENSE) {
   require(mainGame->dField.mzone[0][0]->owner==1 && mainGame->dField.mzone[0][0]->controler==0,"Saving existing card preserves opponent ownership and chosen control");
   require(mainGame->dField.mzone[0][0]->cmdFlag&COMMAND_REPOS,"Existing-card edit clears current-turn summon restriction");
   auto* image=mainGame->driver->createScreenShot();if(image){require(mainGame->driver->writeImageToFile(image,"../editor-preview.png"),"Existing-card edit and deletion form renders");image->drop();}
   auto* controller=static_cast<irr::gui::IGUIComboBox*>(element(9011));controller->setSelected(1);
   irr::SEvent event{};event.EventType=irr::EET_GUI_EVENT;event.GUIEvent.EventType=irr::gui::EGET_COMBO_BOX_CHANGED;event.GUIEvent.Caller=controller;mainGame->device->postEventFromUser(event);
   require(!static_cast<irr::gui::IGUICheckBox*>(element(9018))->isChecked(),"Changing selected controller preserves original owner in edit form");
   click(9003);stage=18;last=frames;return;
  }
  if(stage==18 && ready && !mainGame->dField.mzone[0][0] && mainGame->dField.mzone[1][0]) {
   require(mainGame->dField.mzone[1][0]->owner==1,"GUI save transfers selected existing card while preserving owner");click(9007);stage=19;last=frames;return;
  }
  if(stage==19 && ready && mainGame->dField.mzone[0][0] && !mainGame->dField.mzone[1][0]) {
   require(mainGame->dField.mzone[0][0]->owner==1,"Undo restores existing card's previous controller");click(9017);stage=16;last=frames;return;
  }
  if(stage==16 && ready && !mainGame->dField.mzone[0][0]) {
   require(true,"GUI delete removes selected card from the board");click(9007);stage=17;last=frames;return;
  }
  if(stage==17 && ready && mainGame->dField.mzone[0][0]) {
   require(mainGame->dField.mzone[0][0]->owner==1 && mainGame->dField.mzone[0][0]->controler==0,"GUI undo restores deleted card with control change intact");
   require(DuelEditor::OpenCard(0,LOCATION_MZONE,0),"Restored card can be selected for editing again");click(9017);stage=14;last=frames;return;
  }
  if(stage==14 && ready && !mainGame->dField.mzone[0][0]) {
   require(true,"GUI undo removes monster with its summon-turn state");click(9004);click(BUTTON_LEAVE_GAME);stage=11;last=frames;return;
  }
  if(stage==11 && !DuelEditor::active && !mainGame->dInfo.isStarted && mainGame->wMainMenu->isVisible()) {
   require(true,"Repeated start and exit work without stuck input signals");
   click(BUTTON_REPLAY_MODE);mainGame->open_file=true;
   BufferIO::CopyWideString(L"./replay/editor-engine-smoke.yrp",mainGame->open_file_name);
   click(BUTTON_LOAD_REPLAY);stage=12;last=frames;return;
  }
  if(stage==12 && mainGame->dInfo.isReplay && mainGame->wMessage->isVisible()) {
   require(mainGame->dInfo.lp[1]==6500 && mainGame->dField.hand[0].size()==2 && mainGame->dField.grave[0].size()==1,"Actual GUI replay player reproduces edited final LP and draw effect");
   stage=34;last=frames;return;
  }
  if(stage==34 && frames>last+16) {click(BUTTON_MSG_OK);stage=32;last=frames;return;}
  if(stage==32 && !mainGame->dInfo.isStarted && mainGame->wReplay->isVisible() && frames>last+12) {
   mainGame->open_file=true;BufferIO::CopyWideString(L"./replay/editor-deck-smoke.yrp",mainGame->open_file_name);click(BUTTON_LOAD_REPLAY);stage=33;last=frames;return;
  }
  if(stage==33 && mainGame->dInfo.isReplay && mainGame->wMessage->isVisible()) {
   require(mainGame->dField.deck[0].size()==65 && mainGame->dField.deck[1].size()==3 && mainGame->dField.extra[0].size()==20,"Actual GUI player restores embedded unchecked decks and sorted replay");
   ReplayMode::StopReplay(true);success=true;mainGame->device->closeDevice();
  }
 } catch(const std::exception& e) {
  std::cerr<<"FAIL "<<e.what()<<"; curMsg="<<int(mainGame->dInfo.curMsg)<<"; stage="<<stage<<std::endl;
  SingleMode::StopPlay(true);mainGame->device->closeDevice();
 }
}
}
int main() {
 using namespace ygo;
 std::setlocale(LC_CTYPE,".UTF-8");
 try {
  mainGame=new Game();require(mainGame->Initialize(),"Original OpenGL client initializes with packaged resources");
  ShowWindow(mainGame->hWnd,SW_HIDE);mainGame->MainLoop();
  if(success)std::cout<<"SUCCESS GUI editor, effect, undo and exit smoke test"<<std::endl;
  return success?0:1;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
