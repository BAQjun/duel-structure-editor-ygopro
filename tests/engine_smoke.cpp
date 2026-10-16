#include "../upstream/ocgcore/editor.h"
#include "../upstream/ocgcore/duel.h"
#include "../upstream/ocgcore/field.h"
#include "../upstream/ocgcore/card.h"
#include "../upstream/ocgcore/effect.h"
#include "../upstream/ocgcore/interpreter.h"
#include "../upstream/gframe/replay.h"
#include "../upstream/gframe/editor_history.h"
#include "../upstream/gframe/editor_setup.h"
#include "../upstream/gframe/editor_timeline.h"
#undef MAX_CARD_ID
#include "../upstream/gframe/data_manager.h"
#include <sqlite3.h>
#include <irrlicht.h>
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <fstream>
#include <algorithm>
using namespace ygo;
static int assertions=0, scriptErrors=0;
static Replay* recorder=nullptr;
static EditorHistory history;
static std::vector<std::string> checkpoints;
static uint32_t seed[SEED_COUNT]={1234,5,6,7,8,9,10,11};
void check(bool value,const std::string& message) {
 if(!value)throw std::runtime_error(message);
 ++assertions;std::cout<<"PASS "<<message<<std::endl;
}
uint32_t logMessage(intptr_t d,uint32_t type) {
 char msg[256]{};get_log_message(d,msg);
 if(type!=2)++scriptErrors;
 std::cerr<<"SCRIPT "<<msg<<std::endl;return 0;
}
std::string snapshot(intptr_t h) {
 auto* f=reinterpret_cast<duel*>(h)->game_field;
 std::ostringstream s;
 s<<f->infos.turn_id<<','<<int(f->infos.turn_player)<<','<<f->infos.phase<<','<<editor_revision(h);
 for(int p=0;p<2;++p) {
  s<<"|"<<f->player[p].lp<<','<<f->core.summon_count[p];
  for(auto loc:{LOCATION_DECK,LOCATION_HAND,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE,LOCATION_REMOVED,LOCATION_EXTRA}) {
   s<<'/'<<loc<<':';
   for(int i=0;i<255;++i) {
    auto* c=f->get_field_card(p,loc,i);
    if(c)s<<i<<','<<c->data.code<<','<<int(c->owner)<<','<<int(c->current.position)<<','<<c->cardid<<','<<c->is_status(STATUS_PROC_COMPLETE)<<','<<c->status<<','<<c->turnid<<','<<c->summon_info<<','<<int(c->summon_player)<<';';
   }
  }
 }
 return s.str();
}
void response(intptr_t h,int32_t value) {
 if(recorder){auto bytes=recorder->WriteResponse(&value,4);history.RecordResponse(reinterpret_cast<unsigned char*>(&value),4,bytes);}
 set_responsei(h,value);
}
void responseBytes(intptr_t h,uint8_t* value,size_t size) {
 if(recorder){auto bytes=recorder->WriteResponse(value,size);history.RecordResponse(value,size,bytes);}
 set_responseb(h,value);
}
uint32_t pump(intptr_t h) {
 std::array<uint8_t,SIZE_MESSAGE_BUFFER> buf{};
 for(int i=0;i<20000;++i) {
  auto state=process(h);
  if(state&PROCESSOR_BUFFER_LEN)get_message(h,buf.data());
  if((state&PROCESSOR_FLAG)==PROCESSOR_END)throw std::runtime_error("Duel unexpectedly ended");
  if((state&PROCESSOR_FLAG)==PROCESSOR_WAITING)return state;
 }
 throw std::runtime_error("Engine did not reach an input boundary");
}
void toIdle(intptr_t h) {
 auto* f=reinterpret_cast<duel*>(h)->game_field;
 for(int i=0;i<100;++i) {
  pump(h);const auto& top=f->core.units.front();
  if(top.type==PROCESSOR_SELECT_IDLECMD)return;
  check(!editor_can_edit(h),"Effect/chain/selection boundary rejects editing");
  switch(top.type) {
  case PROCESSOR_SELECT_CHAIN:response(h,-1);break;
  case PROCESSOR_SELECT_EFFECTYN:case PROCESSOR_SELECT_YESNO:case PROCESSOR_SELECT_OPTION:response(h,0);break;
  case PROCESSOR_SELECT_POSITION:response(h,POS_FACEUP_ATTACK);break;
  case PROCESSOR_SELECT_PLACE: {
   std::array<uint8_t,SIZE_RETURN_VALUE> place{};
   bool found=false;
   for(int side=0;side<2&&!found;++side)for(int loc=0;loc<2&&!found;++loc)for(int seq=0;seq<(loc?8:7);++seq) {
    int bit=side*16+loc*8+seq;
    if(!(top.arg2&(1u<<bit))) {
     place[0]=uint8_t(side?1-top.arg1:top.arg1);place[1]=loc?LOCATION_SZONE:LOCATION_MZONE;place[2]=uint8_t(seq);found=true;break;
    }
   }
   if(!found)throw std::runtime_error("No legal test placement");
   responseBytes(h,place.data(),3);break;
  }
  default:throw std::runtime_error("Unsupported test choice processor: "+std::to_string(top.type));
  }
 }
 throw std::runtime_error("Did not return to idle");
}
intptr_t create(const char* scenario="./single/duel_editor.lua") {
 intptr_t h=create_duel_v2(seed);
 editor_enable(h,1);
 check(preload_script(h,scenario)!=0,"Initial scenario loads");
 std::array<uint8_t,SIZE_MESSAGE_BUFFER> buf{};get_message(h,buf.data());
 start_duel(h,0);toIdle(h);
 return h;
}
void recordHeader(Replay& rec,bool extended,const EditorSetup* setup=nullptr,uint32_t format=4) {
 ExtendedReplayHeader rh{};rh.base.id=REPLAY_ID_YRP2;rh.base.version=0x1362;
 rh.base.flag=REPLAY_SINGLE_MODE|REPLAY_UNIFORM|(extended?REPLAY_EDITOR:0);rh.value1=extended?format:0;
 std::copy(seed,seed+SEED_COUNT,rh.seed_sequence);
 rec.BeginRecord();rec.WriteHeader(rh);
 uint16_t name[20]{};rec.WriteData(name,sizeof name,false);rec.WriteData(name,sizeof name,false);
 rec.WriteInt32(8000,false);rec.WriteInt32(0,false);rec.WriteInt32(1,false);rec.WriteInt32(0,false);
 const char* script="./single/duel_editor.lua";
 rec.Write<uint16_t>(uint16_t(std::strlen(script)),false);rec.WriteData(script,std::strlen(script));
 EditorSetup fallback;if(!setup){fallback.decks[0].main.assign(40,89631139);fallback.decks[1].main.assign(40,89631139);setup=&fallback;}
 if(extended && format>=3){rec.WriteInt32(setup->hand,false);if(format>=4)rec.WriteInt32(setup->hand1==0xffffffffU?setup->hand:setup->hand1,false);for(const auto& deck:setup->decks)for(const auto* list:{&deck.main,&deck.extra}){rec.WriteInt32(list->size(),false);rec.WriteData(list->data(),list->size()*4,false);}}
}
void edit(intptr_t h,editor_command command) {
 command.revision=editor_revision(h);
 check(editor_can_edit(h),"Main-phase action boundary permits editing");
 check(recorder->CanWriteEditorCommand(),"Edit has recorder capacity");
 check(editor_apply(h,&command)==EDITOR_OK,"Engine accepts edit command");
 check(recorder->WriteEditorCommand(command),"Edit command recorded");history.RecordEdit(command,sizeof(editor_command)+2);
 toIdle(h);checkpoints.push_back(snapshot(h));history.MarkCheckpoint(h);
}
int main() {
 try {
  auto* nullDevice=irr::createDevice(irr::video::EDT_NULL);
  dataManager.IrrFileSystem=nullDevice->getFileSystem();
  check(dataManager.LoadDB("cards.cdb"),"Real local card database loads");
  set_card_reader(DataManager::CardReader);set_script_reader(default_script_reader);set_message_handler(logMessage);
  intptr_t h=create();auto* f=reinterpret_cast<duel*>(h)->game_field;
  check(editor_can_edit(h),"Fresh main-phase boundary is safe");
  Replay rec;recordHeader(rec,true);recorder=&rec;
  history.Reset(seed,"./single/duel_editor.lua",0);history.MarkCheckpoint(h);
  const auto initialState=snapshot(h);
  editor_command cmd{};cmd.opcode=1;cmd.code=55144522;cmd.location=LOCATION_HAND;
  editor_enable(h,0);check(editor_apply(h,&cmd)==EDITOR_DISABLED,"Normal sessions reject editor API");editor_enable(h,1);
  auto before=snapshot(h);cmd.code=1;
  check(editor_apply(h,&cmd)==EDITOR_UNKNOWN_CARD,"Unknown card rejected");check(before==snapshot(h),"Rejected edit preserves state");
  cmd.code=55144522;cmd.revision=900;
  check(editor_apply(h,&cmd)==EDITOR_STALE,"Stale revision rejected");cmd.revision=0;
  edit(h,cmd);
  check(f->player[0].list_hand.size()==1,"Generated card enters player 1 hand");
  int activate=-1;
  for(size_t i=0;i<f->core.select_chains.size();++i)if(f->core.select_chains[i].triggering_effect->get_handler()->data.code==55144522)activate=int(i);
  check(activate>=0,"Generated Pot of Greed appears in legal activations");
  response(h,5|(activate<<16));toIdle(h);checkpoints.push_back(snapshot(h));history.MarkCheckpoint(h);
  check(f->player[0].list_hand.size()==2,"Real Pot of Greed Lua effect draws two cards");
  check(f->player[0].list_grave.size()==1 && f->player[0].list_grave[0]->data.code==55144522,"Activated spell reaches grave normally");
  uint32_t vanilla=0;
  for(const auto& entry:dataManager.GetDataTable())if(entry.second.type==(TYPE_MONSTER|TYPE_NORMAL)&&entry.second.level==4){vanilla=entry.first;break;}
  check(vanilla!=0,"Level 4 normal monster found in database");
  cmd.code=vanilla;edit(h,cmd);
  auto it=std::find_if(f->core.summonable_cards.begin(),f->core.summonable_cards.end(),[&](card* c){return c->data.code==vanilla;});
  check(it!=f->core.summonable_cards.end(),"Generated monster becomes normally summonable");
  response(h,int(it-f->core.summonable_cards.begin())<<16);toIdle(h);checkpoints.push_back(snapshot(h));history.MarkCheckpoint(h);
  check(f->core.summon_count[0]==1 && f->player[0].list_mzone[0],"Normal summon completes and consumes summon allowance");
  cmd.player=1;cmd.owner=1;cmd.code=55144522;edit(h,cmd);
  check(f->player[1].list_hand.size()==1,"Player 2 hand is editable from player 1 turn");
  cmd.player=0;cmd.owner=0;cmd.code=vanilla;edit(h,cmd);
  check(f->core.summon_count[0]==1,"Repeated edits preserve used normal summon allowance");
  check(std::none_of(f->core.summonable_cards.begin(),f->core.summonable_cards.end(),[&](card* c){return c->data.code==vanilla;}),"Editing does not restore illegal additional normal summon");
  cmd.location=LOCATION_MZONE;cmd.sequence=1;cmd.position=POS_FACEUP_ATTACK;cmd.code=89631139;edit(h,cmd);
  check(!f->player[0].list_mzone[1]->is_status(STATUS_PROC_COMPLETE),"Manual placement does not imply completed summon procedure");
  before=snapshot(h);cmd.revision=editor_revision(h);
  check(editor_apply(h,&cmd)==EDITOR_OCCUPIED,"Occupied monster zone rejects creation");check(snapshot(h)==before,"Occupied zone rejection leaves duel unchanged");
  cmd.opcode=2;cmd.player=1;cmd.value=6500;edit(h,cmd);
  check(f->player[1].lp==6500 && f->infos.turn_id==1 && f->infos.phase==PHASE_MAIN1,"LP edit preserves current turn and phase");
  int undos=0;
  while(history.CanUndo()) {
   check(history.Undo(h,rec),"Undo reconstructs previous checkpoint and truncates production replay");
   toIdle(h);checkpoints.pop_back();++undos;
   const auto expected=checkpoints.empty()?initialState:checkpoints.back();
   check(snapshot(h)==expected,"Undo restores cards, LP, phase, identities and summon allowance exactly");
  }
  check(undos==8,"All eight completed edits/actions can be undone individually");
  f=reinterpret_cast<duel*>(h)->game_field;
  check(f->player[0].list_hand.empty()&&f->player[0].list_grave.empty()&&f->core.summon_count[0]==0,"Undoing draw effect and normal summon restores original engine state");
  check(!history.Undo(h,rec),"Undo at initial state is rejected");
  cmd=editor_command{};cmd.opcode=1;cmd.code=55144522;cmd.location=LOCATION_HAND;edit(h,cmd);
  activate=-1;
  for(size_t i=0;i<f->core.select_chains.size();++i)if(f->core.select_chains[i].triggering_effect->get_handler()->data.code==55144522)activate=int(i);
  check(activate>=0,"A new branch can activate a card after undo");
  response(h,5|(activate<<16));toIdle(h);checkpoints.push_back(snapshot(h));history.MarkCheckpoint(h);
  cmd.opcode=2;cmd.player=1;cmd.value=6500;edit(h,cmd);
  for(uint32_t type:{TYPE_RITUAL,TYPE_FUSION,TYPE_SYNCHRO,TYPE_XYZ,TYPE_LINK,TYPE_SPSUMMON,TYPE_TOKEN}) {
   const CardDataC* selected=nullptr;
   for(const auto& row:dataManager.GetDataTable())if((row.second.type&TYPE_MONSTER) && (row.second.type&type)){selected=&row.second;break;}
   if(!selected)continue;
   editor_command bad{};bad.opcode=1;bad.code=selected->code;bad.location=LOCATION_MZONE;bad.value=EDITOR_SUMMON_NORMAL;bad.revision=editor_revision(h);
   auto unchanged=snapshot(h);
   check(editor_apply(h,&bad)==EDITOR_BAD_DESTINATION,"Intrinsically non-normal-summonable monster rejects normal summon history");
   check(snapshot(h)==unchanged,"Rejected summon history does not mutate duel");
  }
  cmd=editor_command{};cmd.opcode=1;cmd.code=vanilla;cmd.location=LOCATION_MZONE;cmd.value=EDITOR_SUMMON_NORMAL;
  edit(h,cmd);auto* normal=f->player[0].list_mzone[0];
  check(normal->is_status(STATUS_SUMMON_TURN) && normal->turnid==f->infos.turn_id,"Generated normal summon carries current-turn status and turn ID");
  check(!normal->is_capable_change_position(0),"Current-turn summon cannot manually change battle position");
  check(f->core.summon_count[0]==0,"Historical summon marker does not execute an extra summon action");
  cmd.code=89631139;cmd.sequence=1;cmd.value=EDITOR_SUMMON_SPECIAL|EDITOR_PROC_COMPLETE;edit(h,cmd);
  check(f->player[0].list_mzone[1]->is_status(STATUS_SPSUMMON_TURN|STATUS_PROC_COMPLETE),"Special-summon turn flag and completed procedure are independent flags");
  cmd.code=vanilla;cmd.sequence=2;cmd.value=EDITOR_SUMMON_FLIP;edit(h,cmd);
  check(f->player[0].list_mzone[2]->is_status(STATUS_FLIP_SUMMON_TURN),"Flip-summon current-turn status reaches engine");
  check(history.Undo(h,rec),"Undo restores previous summon-turn state");toIdle(h);checkpoints.pop_back();f=reinterpret_cast<duel*>(h)->game_field;
  check(!f->player[0].list_mzone[2] && f->player[0].list_mzone[0]->is_status(STATUS_SUMMON_TURN),"Undo preserves prior monster summon metadata");
  edit(h,cmd);
  cmd=editor_command{};cmd.opcode=1;cmd.code=vanilla;cmd.location=LOCATION_MZONE;cmd.sequence=3;cmd.owner=1;edit(h,cmd);
  auto* foreign=f->player[0].list_mzone[3];uint64_t foreignID=foreign->cardid;
  check(foreign->owner==1 && foreign->current.controler==0,"Opponent-owned generated monster keeps chosen controller after adjustment");
  check(std::get<uint8_t>(foreign->refresh_control_status())==0,"Generated ownership mismatch has a real control-change effect");
  std::vector<editor_card_info> listed(editor_list_cards(h,nullptr,0));editor_list_cards(h,listed.data(),listed.size());
  auto info=std::find_if(listed.begin(),listed.end(),[&](const editor_card_info& c){return c.id==foreignID;});
  check(info!=listed.end() && info->owner==1 && info->player==0,"Existing-card data distinguishes stable identity, owner and controller");
  cmd.opcode=4;cmd.target_low=uint32_t(foreignID);cmd.target_high=uint32_t(foreignID>>32);cmd.player=1;cmd.owner=1;cmd.sequence=0;cmd.position=POS_FACEUP_DEFENSE;cmd.value=EDITOR_SUMMON_NORMAL;edit(h,cmd);
  check(f->player[1].list_mzone[0]==foreign && foreign->cardid==foreignID,"Editing existing card moves the same instance to selected controller");
  check(foreign->current.position==POS_FACEUP_DEFENSE && foreign->is_status(STATUS_SUMMON_TURN),"Existing-card edit updates position and summon-turn state");
  editor_command occupied=cmd;occupied.player=0;occupied.owner=1;occupied.sequence=0;occupied.revision=editor_revision(h);before=snapshot(h);
  check(editor_apply(h,&occupied)==EDITOR_OCCUPIED && snapshot(h)==before,"Editing into an occupied zone is rejected atomically");
  cmd.opcode=3;edit(h,cmd);
  check(!f->player[1].list_mzone[0] && foreign->current.location==0,"Delete removes existing card from active duel without freeing referenced object");
  editor_command staleDelete=cmd;staleDelete.revision=editor_revision(h);
  check(editor_apply(h,&staleDelete)==EDITOR_BAD_COMMAND,"Deleted stable identity cannot accidentally delete a different card");
  check(history.Undo(h,rec),"Deleted card can be restored by undo");toIdle(h);checkpoints.pop_back();f=reinterpret_cast<duel*>(h)->game_field;
  check(f->player[1].list_mzone[0] && f->player[1].list_mzone[0]->cardid==foreignID && f->player[1].list_mzone[0]->owner==1,"Undo restores deleted card identity and ownership");
  edit(h,cmd);
  // Duplicate printed codes must remain separate targets even after hand sequences shift.
  cmd=editor_command{};cmd.opcode=1;cmd.code=vanilla;cmd.location=LOCATION_HAND;edit(h,cmd);edit(h,cmd);
  auto handCount=f->player[0].list_hand.size();auto* lastCard=f->player[0].list_hand.back();auto* other=f->player[0].list_hand[handCount-2];uint64_t otherID=other->cardid;
  cmd.opcode=3;cmd.target_low=uint32_t(lastCard->cardid);cmd.target_high=uint32_t(lastCard->cardid>>32);edit(h,cmd);
  check(f->player[0].list_hand.size()==handCount-1 && other->current.location==LOCATION_HAND,"Stable-ID deletion selects one of two identical cards");
  check(history.Undo(h,rec),"Undo restores one deleted duplicate");toIdle(h);checkpoints.pop_back();f=reinterpret_cast<duel*>(h)->game_field;
  check(f->player[0].list_hand.size()==handCount,"Duplicate hand deletion rollback restores exact count");
  // Remove both test copies so the existing GUI replay expectation still checks the draw result.
  edit(h,cmd);
  cmd.target_low=uint32_t(otherID);cmd.target_high=uint32_t(otherID>>32);edit(h,cmd);
  auto finalState=snapshot(h);int randomNext=reinterpret_cast<duel*>(h)->get_next_integer(1,1000000);
  rec.EndRecord();check(rec.SaveReplay(L"editor-engine-smoke"),"Edited replay saved using production recorder");
  recorder=nullptr;end_duel(h);
  Replay played;check(played.OpenReplay(L"editor-engine-smoke.yrp"),"Edited replay loads using production parser");played.SkipInfo();
  h=create();size_t checkpoint=0, events=0;
  std::array<uint8_t,SIZE_RETURN_VALUE> resp{};editor_command saved{};bool isEdit=false;
  while(played.ReadNextInput(resp.data(),saved,isEdit)) {
   if(isEdit)check(editor_apply(h,&saved)==EDITOR_OK,"Replay reapplies recorded edit at correct revision");
   else set_responseb(h,resp.data());
   pump(h);
   if(reinterpret_cast<duel*>(h)->game_field->core.units.front().type==PROCESSOR_SELECT_IDLECMD) {
    check(checkpoint<checkpoints.size() && snapshot(h)==checkpoints[checkpoint],"Replay idle checkpoint matches recorded engine state");
    ++checkpoint;
   }
   ++events;
  }
  check(checkpoint==checkpoints.size(),"All recording checkpoints reproduced");
  check(finalState==snapshot(h),"Replay final cards, LP, phase, identity and summon count match recording");
  check(randomNext==reinterpret_cast<duel*>(h)->get_next_integer(1,1000000),"Replay retains identical PRNG state");end_duel(h);
  Replay legacy;recordHeader(legacy,false);int32_t value=7;legacy.WriteResponse(&value,4);legacy.EndRecord();check(legacy.SaveReplay(L"legacy-parser-smoke"),"Legacy replay fixture saves");
  Replay old;check(old.OpenReplay(L"legacy-parser-smoke.yrp"),"Legacy replay loads");old.SkipInfo();
  check(old.ReadNextResponse(resp.data()) && std::memcmp(resp.data(),&value,4)==0,"Legacy response format remains readable");
  Replay oldEditor;recordHeader(oldEditor,true,nullptr,1);oldEditor.pheader.value1=1;
  uint32_t oldWords[10]={1,1,0,55144522,0,0,LOCATION_HAND,0,POS_FACEUP_ATTACK,0};
  oldEditor.Write<uint8_t>(0,false);oldEditor.Write<uint8_t>(1,false);oldEditor.WriteData(oldWords,sizeof(oldWords));oldEditor.EndRecord();check(oldEditor.SaveReplay(L"old-editor-format-smoke"),"Version-1 edit fixture saves");
  Replay version1;check(version1.OpenReplay(L"old-editor-format-smoke.yrp"),"Version-1 edited replay opens");version1.SkipInfo();
  check(version1.ReadNextInput(resp.data(),saved,isEdit) && isEdit && saved.version==1 && saved.code==55144522 && saved.target_low==0,"Old 40-byte edit records remain readable after stable-ID extension");

  const char* relationsScenario="../../work/card-relations-test.lua";
  {
   std::ofstream fixture(relationsScenario);
   fixture<<"Debug.ReloadFieldBegin(DUEL_ATTACK_FIRST_TURN,5)\nDebug.SetPlayerInfo(0,8000,0,1)\nDebug.SetPlayerInfo(1,8000,0,1)\n";
   fixture<<"for p=0,1 do for i=1,40 do Debug.AddCard(89631139,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE) end end\n";
   fixture<<"local xyz=Debug.AddCard(84013237,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK)\n";
   fixture<<"Debug.AddCard("<<vanilla<<",0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK)\n";
   fixture<<"local axe=Debug.AddCard(40619825,0,0,LOCATION_SZONE,0,POS_FACEUP_ATTACK)\nDebug.PreEquip(axe,xyz)\nDebug.ReloadFieldEnd()\n";
  }
  h=create(relationsScenario);f=reinterpret_cast<duel*>(h)->game_field;
  auto* xyz=f->player[0].list_mzone[0];auto* axe=f->player[0].list_szone[0];
  check(xyz && axe && xyz->xyz_materials.size()==1 && axe->equiping_target==xyz,"Relationship fixture contains XYZ material and real equip relation");
  int equippedAttack=xyz->get_attack();
  editor_command removeEquip{};removeEquip.opcode=3;removeEquip.target_low=uint32_t(axe->cardid);removeEquip.target_high=uint32_t(axe->cardid>>32);
  check(editor_apply(h,&removeEquip)==EDITOR_OK,"Deleting equipped card is supported");toIdle(h);
  check(!f->player[0].list_szone[0] && xyz->equiping_cards.empty() && xyz->get_attack()<equippedAttack,"Equip deletion clears relation and removes attack modifier");
  auto* material=xyz->xyz_materials[0];editor_command removeMaterial{};removeMaterial.opcode=3;removeMaterial.revision=editor_revision(h);removeMaterial.target_low=uint32_t(material->cardid);removeMaterial.target_high=uint32_t(material->cardid>>32);
  check(editor_apply(h,&removeMaterial)==EDITOR_OK,"Deleting one attached XYZ material by stable identity is supported");toIdle(h);
  check(xyz->xyz_materials.empty() && !material->overlay_target && material->current.location==0,"Material deletion clears both sides of overlay relation");end_duel(h);
  h=create(relationsScenario);f=reinterpret_cast<duel*>(h)->game_field;xyz=f->player[0].list_mzone[0];material=xyz->xyz_materials[0];axe=f->player[0].list_szone[0];
  editor_command removeHost{};removeHost.opcode=3;removeHost.target_low=uint32_t(xyz->cardid);removeHost.target_high=uint32_t(xyz->cardid>>32);
  check(editor_apply(h,&removeHost)==EDITOR_OK,"Deleting XYZ host with equip and material succeeds");toIdle(h);
  check(!f->player[0].list_mzone[0] && xyz->xyz_materials.empty() && material->current.location==0 && !material->overlay_target && !axe->equiping_target,"Host deletion leaves no dangling material or equip relations");end_duel(h);
  {
   std::ofstream fixture("../../work/unchecked-deck.ydk");fixture<<"#main\n55144522\n";for(int i=0;i<64;++i)fixture<<"89631139\n";fixture<<"#extra\n";for(int i=0;i<20;++i)fixture<<"84013237\n";fixture<<"!side\n549481\n";
  }
  EditorSetup setup;std::wstring loadError;
  check(LoadEditorDeck(L"../../work/unchecked-deck.ydk",setup.decks[0],loadError),"Editor loads unchecked YDK with banned and repeated cards");
  check(setup.decks[0].main.size()==65 && setup.decks[0].extra.size()==20 && setup.decks[0].side.empty(),"Unchecked loader preserves over-60 main, over-15 extra and omits unused side");
  setup.decks[1].main={549481,55144522,89631139};
  h=create_duel_v2(seed);editor_enable(h,1);check(setup.Initialize(h),"Selected independent decks initialize without legality checks");
  {std::array<uint8_t,SIZE_MESSAGE_BUFFER> buf{};get_message(h,buf.data());}start_duel(h,0);toIdle(h);f=reinterpret_cast<duel*>(h)->game_field;
  check(f->player[0].list_main.size()==65 && f->player[1].list_main.size()==3 && f->player[0].list_extra.size()==20,"Engine retains independent illegal-sized decks and extras");
  check(f->player[0].list_main.back()->data.code==55144522 && f->player[1].list_main.back()->data.code==549481,"YDK order maps explicitly to top-first deck order");
  Replay deckRec;recordHeader(deckRec,true,&setup);recorder=&deckRec;history.Reset(seed,"./single/duel_editor.lua",0);history.SetInitialSetup(setup);history.MarkCheckpoint(h);
  const auto startDeckState=snapshot(h);auto* top=f->player[0].list_main.back();uint64_t topID=top->cardid;
  editor_command reorder{};reorder.opcode=5;reorder.target_low=uint32_t(topID);reorder.target_high=uint32_t(topID>>32);reorder.sequence=64;
  edit(h,reorder);check(f->player[0].list_main.front()->cardid==topID,"Sorting moves the exact card from top to bottom without changing identity");
  for(size_t i=0;i<f->player[0].list_main.size();++i)check(f->player[0].list_main[i]->current.sequence==i,"Sorting refreshes each underlying deck sequence");
  auto ordered=snapshot(h);reorder.revision=editor_revision(h);reorder.sequence=65;
  check(editor_apply(h,&reorder)==EDITOR_BAD_DESTINATION && snapshot(h)==ordered,"Out-of-range deck reorder rejects atomically");
  check(history.Undo(h,deckRec),"Undo reconstructs selected decks and restores original order");toIdle(h);check(snapshot(h)==startDeckState,"Undo preserves unchecked deck counts, exact order and stable IDs");f=reinterpret_cast<duel*>(h)->game_field;
  reorder.sequence=2;edit(h,reorder);check(f->player[0].list_main[62]->cardid==topID,"Sort can move selected main-deck card to an arbitrary top-based index");
  auto* extraCard=f->player[0].list_extra[0];reorder.target_low=uint32_t(extraCard->cardid);reorder.target_high=uint32_t(extraCard->cardid>>32);reorder.sequence=19;edit(h,reorder);
  check(f->player[0].list_extra.back()==extraCard,"Extra-deck order is independently editable");
  auto deckFinal=snapshot(h);deckRec.EndRecord();check(deckRec.SaveReplay(L"editor-deck-smoke"),"Replay with embedded selected decks is saved");recorder=nullptr;end_duel(h);
  Replay deckPlayed;check(deckPlayed.OpenReplay(L"editor-deck-smoke.yrp"),"Replay version 3 parses embedded selected decks");
  check(deckPlayed.decks.size()==2 && deckPlayed.decks[0].main==setup.decks[0].main && deckPlayed.decks[0].extra==setup.decks[0].extra && deckPlayed.decks[1].main==setup.decks[1].main,"Replay embeds exact initial card lists including order and duplicates");
  EditorSetup restoredSetup;restoredSetup.decks[0]=deckPlayed.decks[0];restoredSetup.decks[1]=deckPlayed.decks[1];restoredSetup.hand=deckPlayed.editor_start_hand;
  h=create_duel_v2(seed);editor_enable(h,1);check(restoredSetup.Initialize(h),"Replay restores setup without original YDK files");{std::array<uint8_t,SIZE_MESSAGE_BUFFER> buf{};get_message(h,buf.data());}start_duel(h,0);toIdle(h);deckPlayed.SkipInfo();
  while(deckPlayed.ReadNextInput(resp.data(),saved,isEdit)){if(isEdit)check(editor_apply(h,&saved)==EDITOR_OK,"Embedded-deck replay applies stable-ID reorder");else set_responseb(h,resp.data());toIdle(h);}
  check(snapshot(h)==deckFinal,"Replayed unchecked decks and all edited orders equal recorded final state");end_duel(h);
  setup.hand=5;h=create_duel_v2(seed);editor_enable(h,1);check(setup.Initialize(h),"Optional starting hand initializes");f=reinterpret_cast<duel*>(h)->game_field;
  check(f->player[0].list_hand.size()==5 && f->player[0].list_main.size()==60 && f->player[1].list_hand.size()==3 && f->player[1].list_main.empty(),"Starting hand takes cards from top in order and clamps to available count");end_duel(h);
  h=create();f=reinterpret_cast<duel*>(h)->game_field;
  auto apply=[&](editor_command cmd){cmd.revision=editor_revision(h);check(editor_apply(h,&cmd)==EDITOR_OK,"Configuration command applies in live engine");toIdle(h);};
  editor_command form{};form.opcode=1;form.code=89631139;form.location=LOCATION_SZONE;form.form=1;apply(form);
  auto* configured=f->player[0].list_szone[0];check(configured && configured->get_type()==(TYPE_SPELL|TYPE_CONTINUOUS),"Monster stays in spell/trap zone as a continuous spell");
  form.sequence=1;form.form=2;apply(form);check(f->player[0].list_szone[1]->get_type()==(TYPE_TRAP|TYPE_CONTINUOUS),"Monster can also become a continuous trap");
  form.code=55144522;form.location=LOCATION_MZONE;form.sequence=0;form.form=3;form.race=RACE_MACHINE;form.attribute=ATTRIBUTE_FIRE;form.attack=2300;form.defense=1200;form.level=6;form.monster_flags=1;apply(form);
  configured=f->player[0].list_mzone[0];check(configured && configured->get_type()==(TYPE_MONSTER|TYPE_EFFECT) && configured->get_attack()==2300 && configured->get_defense()==1200 && configured->get_race()==RACE_MACHINE && configured->get_attribute()==ATTRIBUTE_FIRE && configured->get_level()==6,"Spell has real custom monster type, race, attribute, level and battle stats");
  form.opcode=4;form.target_low=uint32_t(configured->cardid);form.target_high=uint32_t(configured->cardid>>32);form.monster_flags=2;apply(form);check((configured->get_type()&(TYPE_MONSTER|TYPE_NORMAL|TYPE_SPELL))==(TYPE_MONSTER|TYPE_NORMAL|TYPE_SPELL),"Custom normal monster can retain original spell identity");
  form.restrictions=(1U<<0)|(1U<<2)|(1U<<3)|(1U<<8)|(1U<<12);form.states=STATUS_SET_TURN|STATUS_CANNOT_CHANGE_FORM;apply(form);
  check(configured->is_affected_by_effect(EFFECT_DISABLE) && configured->is_affected_by_effect(EFFECT_CANNOT_TRIGGER) && configured->is_affected_by_effect(EFFECT_CANNOT_ATTACK) && configured->is_affected_by_effect(EFFECT_INDESTRUCTABLE_BATTLE) && configured->is_affected_by_effect(EFFECT_UNRELEASABLE_SUM),"Negation, activation, attack, protection and tribute limits are real effects");
  check(configured->is_status(STATUS_SET_TURN) && configured->is_status(STATUS_CANNOT_CHANGE_FORM),"Editable turn-state flags persist");
  form.restrictions=0;form.states=0;apply(form);check(!configured->is_affected_by_effect(EFFECT_DISABLE) && !configured->is_affected_by_effect(EFFECT_CANNOT_ATTACK),"Clearing editor restrictions removes only tagged effects");
  editor_command additional{};additional.opcode=6;additional.target_low=uint32_t(configured->cardid);additional.target_high=uint32_t(configured->cardid>>32);additional.code=EFFECT_UPDATE_ATTACK;additional.value=500;additional.position=1;apply(additional);check(configured->get_attack()==2800,"Constant-effect editor changes real battle attack");
  additional.code=EFFECT_IMMUNE_EFFECT;additional.value=1;apply(additional);auto* incoming=reinterpret_cast<duel*>(h)->new_effect();incoming->owner=f->player[0].list_szone[0];incoming->handler=incoming->owner;incoming->type=EFFECT_TYPE_SINGLE;incoming->code=EFFECT_UPDATE_ATTACK;
  check(!configured->is_affect_by_effect(incoming),"Constant immunity really rejects an incoming effect");additional.position=0;apply(additional);check(configured->is_affect_by_effect(incoming),"Removing editor immunity restores effect applicability");
  editor_command cannotAct{};cannotAct.opcode=1;cannotAct.code=55144522;cannotAct.location=LOCATION_HAND;apply(cannotAct);check(!f->core.select_chains.empty(),"Unrestricted spell appears in actual legal activation list");
  auto* handSpell=f->player[0].list_hand.back();cannotAct.opcode=4;cannotAct.target_low=uint32_t(handSpell->cardid);cannotAct.target_high=uint32_t(handSpell->cardid>>32);cannotAct.restrictions=1U<<2;apply(cannotAct);check(f->core.select_chains.empty(),"Cannot-trigger state removes real spell activation command");
  cannotAct.restrictions=0;apply(cannotAct);check(!f->core.select_chains.empty(),"Removing cannot-trigger restores legal activation");
  editor_command trap{};trap.opcode=1;trap.code=13955608;trap.location=LOCATION_MZONE;trap.sequence=1;trap.form=4;trap.self_summon=1;trap.value=EDITOR_SUMMON_SPECIAL;apply(trap);
  auto* stronghold=f->player[0].list_mzone[1];check(stronghold && (stronghold->get_type()&(TYPE_MONSTER|TYPE_EFFECT|TYPE_TRAPMONSTER))==(TYPE_MONSTER|TYPE_EFFECT|TYPE_TRAPMONSTER) && stronghold->get_race()==RACE_MACHINE && stronghold->get_attribute()==ATTRIBUTE_EARTH && stronghold->get_attack()==0 && stronghold->get_defense()==2000 && stronghold->get_level()==4,"Native self-summoned trap monster uses actual card-defined properties");
  check((stronghold->summon_info&0xff00ffff)==(SUMMON_TYPE_SPECIAL|1) && stronghold->current.reason_effect && stronghold->current.reason_effect->get_handler()==stronghold,"Trap self-summon retains own summon value and own reason effect");
  trap.code=92092092;trap.sequence=2;apply(trap);auto* tuner=f->player[0].list_mzone[2];check(tuner && (tuner->get_type()&TYPE_TUNER) && tuner->get_defense()==1800 && tuner->get_level()==1,"Own trap profile preserves tuner subtype and original stats");
  trap.code=60433216;trap.sequence=3;apply(trap);auto* legacyTrap=f->player[0].list_mzone[3];check(legacyTrap && (legacyTrap->get_type()&(TYPE_NORMAL|TYPE_TUNER|TYPE_TRAPMONSTER))==(TYPE_NORMAL|TYPE_TUNER|TYPE_TRAPMONSTER) && (legacyTrap->summon_info&0xff00ffff)==SUMMON_TYPE_SPECIAL && legacyTrap->current.reason_effect && legacyTrap->current.reason_effect->get_handler()==legacyTrap,"Legacy trap self-summon preserves actual zero summon value and own effect provenance");
  end_duel(h);
  // Journal every engine process call; points are exact drained message boundaries.
  EditorSetup pointSetup;pointSetup.decks[0].main={55144522,89631139,549481};pointSetup.decks[1].main.assign(40,89631139);pointSetup.hand=1;pointSetup.hand1=3;
  Replay pointRec;recordHeader(pointRec,true,&pointSetup);EditorTimeline points;points.Reset(seed,pointSetup,0,pointRec);
  h=create_duel_v2(seed);editor_enable(h,1);check(pointSetup.Initialize(h),"Independent custom 1/3 starting hands initialize");
  std::array<uint8_t,SIZE_MESSAGE_BUFFER> pointBuffer{};get_message(h,pointBuffer.data());start_duel(h,0);
  auto pointPump=[&](){uint32_t status=0;for(int i=0;i<10000;++i){status=process(h);auto len=status&PROCESSOR_BUFFER_LEN;if(len)get_message(h,pointBuffer.data());points.Processed(h,status,true);if((status&PROCESSOR_FLAG)==PROCESSOR_WAITING){if(len)points.Prompt(pointBuffer.data(),len);return;}}throw std::runtime_error("Timeline pump timeout");};
  pointPump();check(editor_can_edit(h),"Timeline reaches initial editable pause");auto pointState=snapshot(h);
  editor_command changeLP{};changeLP.opcode=2;changeLP.player=1;changeLP.value=6400;changeLP.revision=editor_revision(h);
  check(editor_apply(h,&changeLP)==EDITOR_OK,"Timeline edit accepted");points.Edit(changeLP);pointRec.WriteEditorCommand(changeLP);pointPump();auto afterLP=snapshot(h);
  check(points.CanBack(),"Timeline exposes prior message point");check(points.Back(h,pointRec),"Back reconstructs one exact message point including RNG");
  check(points.CanForward() && points.Forward(h,pointRec) && snapshot(h)==afterLP,"Forward reconstructs retained next point without a new choice");
  check(points.Save("../../work/point-save.yges","Native test save"),"Persistent point snapshot saves journal, setup and prompt");
  EditorTimeline loaded;check(loaded.Load("../../work/point-save.yges",h,pointRec) && snapshot(h)==afterLP,"Saved exact point loads into a live engine and recorder");
  check(loaded.CanBack(),"Loaded save retains backward point history");
  {std::ofstream bad("../../work/point-save-bad.yges",std::ios::binary);bad<<"bad";}auto safe=snapshot(h);check(!loaded.Load("../../work/point-save-bad.yges",h,pointRec) && snapshot(h)==safe,"Invalid save fails without replacing current duel");

  // Resume a real Lua effect, then save and restore its suspended operation point.
  points=std::move(loaded);f=reinterpret_cast<duel*>(h)->game_field;
  int32_t pointActivation=5;points.Response(reinterpret_cast<uint8_t*>(&pointActivation),4);pointRec.WriteResponse(&pointActivation,4);set_responsei(h,pointActivation);
  bool coroutineSaved=false;
  for(int iteration=0;iteration<1000;++iteration){
   uint32_t status=process(h);auto size=status&PROCESSOR_BUFFER_LEN;if(size)get_message(h,pointBuffer.data());points.Processed(h,status,true);
   const bool waiting=(status&PROCESSOR_FLAG)==PROCESSOR_WAITING;if(waiting && size)points.Prompt(pointBuffer.data(),size);
   auto* d=reinterpret_cast<duel*>(h);f=d->game_field;
   if(!coroutineSaved && !waiting && !d->lua->coroutines.empty()){
    auto beforeCoroutine=EditorHistory::Snapshot(h);check(points.Save("../../work/coroutine-save.yges","In-flight Lua effect"),"Can save a message point with a suspended real Lua effect");
    EditorTimeline resumed;check(resumed.Load("../../work/coroutine-save.yges",h,pointRec) && EditorHistory::Snapshot(h)==beforeCoroutine,"Load restores suspended Lua execution, queues, effects and RNG");points=std::move(resumed);d=reinterpret_cast<duel*>(h);f=d->game_field;coroutineSaved=true;
   }
   if(waiting){if(editor_can_edit(h))break;const auto& request=f->core.units.front();std::array<uint8_t,SIZE_RETURN_VALUE> responseData{};size_t length=4;int32_t value=0;
    if(request.type==PROCESSOR_SELECT_CHAIN)value=-1;
    else if(request.type==PROCESSOR_SELECT_PLACE){length=3;bool found=false;for(int p=0;p<2&&!found;++p)for(int z=0;z<2&&!found;++z)for(int i=0;i<(z?8:7);++i)if(!(request.arg2&(1U<<(p*16+z*8+i)))){responseData[0]=p?1-request.arg1:request.arg1;responseData[1]=z?LOCATION_SZONE:LOCATION_MZONE;responseData[2]=i;found=true;break;}check(found,"Effect journal supplies legal placement");}
    else if(request.type==PROCESSOR_SELECT_EFFECTYN || request.type==PROCESSOR_SELECT_YESNO)value=0;
    else throw std::runtime_error("Unexpected effect journal request");
    if(length==4)std::memcpy(responseData.data(),&value,4);points.Response(responseData.data(),length);pointRec.WriteResponse(responseData.data(),length);set_responseb(h,responseData.data());
   }
  }
  check(coroutineSaved && editor_can_edit(h) && f->player[0].list_hand.size()==2 && f->player[0].list_grave.size()==1,"Loaded in-flight effect continues to normal Pot of Greed resolution");
  end_duel(h);pointRec.EndRecord();
  EditorSetup earlySetup;for(auto& deck:earlySetup.decks)deck.main.assign(40,89631139);earlySetup.hand=0;earlySetup.hand1=0;
  Replay earlyRec;recordHeader(earlyRec,true,&earlySetup,5);EditorTimeline earlyPoints;earlyPoints.Reset(seed,earlySetup,0,earlyRec);
  h=create_duel_v2(seed);editor_enable(h,2);check(earlySetup.Initialize(h),"Pre-draw setup initializes");get_message(h,pointBuffer.data());start_duel(h,0);earlyPoints.InitialPoint(h);
  f=reinterpret_cast<duel*>(h)->game_field;
  check(editor_can_edit(h) && f->infos.turn_id==0,"Editor is writable before first turn or draw");
  check(earlyPoints.Save("../../work/pre-draw.yges","Before draw"),"Initial pre-process checkpoint can be saved");
  editor_command earlyLP{};earlyLP.opcode=2;earlyLP.value=7900;earlyLP.revision=editor_revision(h);
  check(editor_apply(h,&earlyLP)==EDITOR_OK,"Initial position accepts edit");earlyPoints.Edit(earlyLP);earlyRec.WriteEditorCommand(earlyLP,0);
  auto earlyResult=process(h);get_message(h,pointBuffer.data());earlyPoints.Processed(h,earlyResult,true);
  check(f->infos.turn_id==0 && f->player[0].list_hand.empty() && editor_can_edit(h),"Publishing initial edit does not start turn or draw");
  check(earlyPoints.Back(h,earlyRec) && reinterpret_cast<duel*>(h)->game_field->player[0].lp==8000,"Back returns to initial pre-draw checkpoint");
  check(earlyPoints.Forward(h,earlyRec) && reinterpret_cast<duel*>(h)->game_field->player[0].lp==7900,"Forward rebuilds pre-draw edits exactly");
  check(earlyPoints.Load("../../work/pre-draw.yges",h,earlyRec) && editor_can_edit(h),"Loading pre-draw save retains writable initial position");
  editor_command beforeDrawCard{};beforeDrawCard.opcode=1;beforeDrawCard.code=89631139;beforeDrawCard.location=LOCATION_GRAVE;beforeDrawCard.position=POS_FACEUP_ATTACK;beforeDrawCard.revision=editor_revision(h);
  check(editor_apply(h,&beforeDrawCard)==EDITOR_OK,"Card can be generated before first turn");
  auto creationResult=process(h);get_message(h,pointBuffer.data());f=reinterpret_cast<duel*>(h)->game_field;
  check(f->infos.turn_id==0 && f->player[0].list_grave.size()==1 && editor_can_edit(h),"Pre-draw generated card is present without advancing time");
  bool drawEdited=false,standbyEdited=false,battleEdited=false,main2Seen=false,endEdited=false,nextTurn=false;
  for(int i=0;i<1000 && !nextTurn;++i){
   auto status=process(h);auto size=status&PROCESSOR_BUFFER_LEN;if(size)get_message(h,pointBuffer.data());f=reinterpret_cast<duel*>(h)->game_field;
   bool* flag=nullptr;const char* label=nullptr;
   if(editor_can_edit(h)){
    if(f->infos.phase==PHASE_DRAW && !drawEdited){flag=&drawEdited;label="Draw phase accepts edits";}
    else if(f->infos.phase==PHASE_STANDBY && !standbyEdited){flag=&standbyEdited;label="Standby phase accepts edits";}
    else if(f->infos.phase==PHASE_BATTLE_STEP && !battleEdited){flag=&battleEdited;label="Battle command point accepts edits";}
    else if(f->infos.phase==PHASE_END && !endEdited){flag=&endEdited;label="End phase accepts edits";}
   }
   if(flag){earlyLP.revision=editor_revision(h);check(editor_apply(h,&earlyLP)==EDITOR_OK,label);*flag=true;continue;}
   if((status&PROCESSOR_FLAG)==PROCESSOR_WAITING){auto type=f->core.units.front().type;
    if(type==PROCESSOR_SELECT_CHAIN)set_responsei(h,-1);
    else if(type==PROCESSOR_SELECT_BATTLECMD)set_responsei(h,2);
    else if(type==PROCESSOR_SELECT_IDLECMD){
     if(f->infos.turn_id>1){nextTurn=true;break;}
     if(f->infos.phase==PHASE_MAIN2){main2Seen=true;set_responsei(h,7);}else set_responsei(h,6);
    }else throw std::runtime_error("Unexpected phase fixture selection");
   }
  }
  check(drawEdited && standbyEdited && battleEdited && main2Seen && endEdited && nextTurn,"Edited duel proceeds through draw, standby, main, battle, main two and next turn");end_duel(h);earlyRec.EndRecord();

  EditorSetup configSetup;configSetup.decks[0].main={89631139,549481,55144522,89631139};configSetup.decks[1].main.assign(40,89631139);configSetup.hand=0;configSetup.hand1=0;
  Replay configRec;recordHeader(configRec,true,&configSetup);EditorTimeline configPoints;configPoints.Reset(seed,configSetup,0,configRec);
  h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"v4 configuration setup initializes");std::array<uint8_t,SIZE_MESSAGE_BUFFER> configBuffer{};get_message(h,configBuffer.data());start_duel(h,0);
  auto configPump=[&](){for(int i=0;i<10000;++i){auto result=process(h);auto len=result&PROCESSOR_BUFFER_LEN;if(len)get_message(h,configBuffer.data());configPoints.Processed(h,result,true);if((result&PROCESSOR_FLAG)==PROCESSOR_WAITING){if(len)configPoints.Prompt(configBuffer.data(),len);return;}}throw std::runtime_error("Configuration timeline timeout");};configPump();
  auto configApply=[&](editor_command c){c.version=4;c.revision=editor_revision(h);check(editor_apply(h,&c)==EDITOR_OK,"v4 edit is accepted by production engine");configPoints.Edit(c);configRec.WriteEditorCommand(c);configPump();};
  auto reject=[&](editor_command c,const char* label){c.revision=editor_revision(h);check(editor_apply(h,&c)!=EDITOR_OK,label);};
  f=reinterpret_cast<duel*>(h)->game_field;std::vector<uint64_t> oldOrder;for(auto* c:f->player[0].list_main)oldOrder.push_back(c->cardid);
  editor_command randomCard{};randomCard.opcode=1;randomCard.code=549481;randomCard.location=LOCATION_DECK;randomCard.sequence=2;configApply(randomCard);f=reinterpret_cast<duel*>(h)->game_field;std::vector<uint64_t> retained;for(auto* c:f->player[0].list_main)if(std::find(oldOrder.begin(),oldOrder.end(),c->cardid)!=oldOrder.end())retained.push_back(c->cardid);check(retained==oldOrder,"Random insertion preserves every other card's relative order");auto randomState=EditorHistory::Snapshot(h);check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==randomState,"Random insertion undo and forward preserve exact order and RNG");
  editor_command custom{};custom.opcode=1;custom.code=89631139;custom.location=LOCATION_MZONE;custom.form=5;custom.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_TUNER;custom.self_summon=549481;custom.attack=2700;custom.defense=1600;custom.race=RACE_MACHINE;custom.attribute=ATTRIBUTE_FIRE;custom.level=6;configApply(custom);f=reinterpret_cast<duel*>(h)->game_field;auto* edited=f->player[0].list_mzone[0];check(edited->get_attack()==2700 && edited->get_defense()==1600 && edited->get_race()==RACE_MACHINE && edited->get_attribute()==ATTRIBUTE_FIRE && edited->get_type()==custom.monster_flags && edited->get_code()==549481,"Custom monster has real stats, race, attribute, tuner type and selected name");check(edited->data.code==89631139,"Name editing preserves original card identity and script");
  custom.opcode=4;custom.target_low=uint32_t(edited->cardid);custom.target_high=uint32_t(edited->cardid>>32);custom.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_XYZ;custom.level=4;configApply(custom);f=reinterpret_cast<duel*>(h)->game_field;edited=f->player[0].list_mzone[0];check(edited->get_rank()==4 && edited->get_level()==0,"Edited Xyz monster exposes rank instead of level");auto configuredState=EditorHistory::Snapshot(h);check(configPoints.Save("../../work/v4-config.yges","v4 custom card"),"Custom properties and random insertion save");check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==configuredState,"Custom type and name reconstruct through undo and forward");check(configPoints.Load("../../work/v4-config.yges",h,configRec) && EditorHistory::Snapshot(h)==configuredState,"Saved custom name, type and stats load exactly");
  custom.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_LINK;custom.position=POS_FACEDOWN_DEFENSE;reject(custom,"Link monster rejects facedown position");custom.position=POS_FACEUP_ATTACK;configApply(custom);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_mzone[0]->get_defense()==0 && f->player[0].list_mzone[0]->get_level()==0,"Edited Link monster has no defense or level");
  editor_command magic{};magic.opcode=1;magic.code=55144522;magic.location=LOCATION_SZONE;reject(magic,"Ordinary spell cannot be placed face-up by editor");magic.position=POS_FACEDOWN_DEFENSE;magic.states=STATUS_SET_TURN;configApply(magic);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_szone[0]->is_status(STATUS_SET_TURN),"Back-row card carries independent set-this-turn history");
  editor_card_info profile{};check(editor_native_trap_profile(13955608,&profile) && profile.defense==2000,"Eligible native trap exposes its actual profile");check(!editor_native_trap_profile(55144522,&profile),"Ordinary spell is not offered native trap shape");
  editor_command converted{};converted.opcode=1;converted.code=55144522;converted.location=LOCATION_MZONE;converted.sequence=1;converted.form=6;converted.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_TUNER|0x80000000U;converted.self_summon=89631139;converted.race=RACE_MACHINE;converted.attribute=ATTRIBUTE_FIRE;converted.attack=700;converted.defense=900;converted.level=3;configApply(converted);f=reinterpret_cast<duel*>(h)->game_field;auto* convertedCard=f->player[0].list_mzone[1];check((convertedCard->get_type()&(TYPE_MONSTER|TYPE_TUNER|TYPE_SPELL))==(TYPE_MONSTER|TYPE_TUNER|TYPE_SPELL) && convertedCard->get_code()==89631139 && convertedCard->get_attack()==700,"Spell monster supports subtype, selected name and optional spell identity");
  uint32_t ritualSpell=0;for(const auto& row:dataManager.GetDataTable())if((row.second.type&(TYPE_SPELL|TYPE_RITUAL))==(TYPE_SPELL|TYPE_RITUAL)){ritualSpell=row.first;break;}editor_command hybrid=converted;hybrid.code=ritualSpell;hybrid.sequence=2;hybrid.value=EDITOR_SUMMON_NORMAL;reject(hybrid,"Retained ritual-spell identity excludes normal-summon history on converted monster");
  custom.monster_flags=TYPE_MONSTER|TYPE_NORMAL|TYPE_FUSION;custom.self_summon=55144522;configApply(custom);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_mzone[0]->get_code()==55144522 && f->player[0].list_mzone[0]->get_type()==custom.monster_flags,"Custom name can use any real card name in library, including spell names");
  auto finalConfigState=EditorHistory::Snapshot(h);configRec.EndRecord();check(configRec.SaveReplay(L"v4-config-smoke"),"v4 card edits save in real replay");end_duel(h);Replay configReplay;check(configReplay.OpenReplay(L"v4-config-smoke.yrp"),"v4 replay opens");configReplay.SkipInfo();int v4Records=0;while(configReplay.ReadNextInput(resp.data(),saved,isEdit))if(isEdit){check(saved.version==4,"Replay parses v4 edit words");++v4Records;}check(v4Records==7,"Replay retains random, monster edits and set history");
  h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"v4 replay reconstructs initial decks");get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);check(configReplay.OpenReplay(L"v4-config-smoke.yrp"),"Replay restarts from its header for full execution");configReplay.SkipInfo();int appliedV4=0;while(configReplay.ReadNextInput(resp.data(),saved,isEdit)){if(isEdit){check(editor_apply(h,&saved)==EDITOR_OK,"Real replay applies custom monster and random insertion edit");++appliedV4;}else set_responseb(h,resp.data());toIdle(h);}check(appliedV4==7 && EditorHistory::Snapshot(h)==finalConfigState,"Real replay reproduces all v4 stats, names, types, deck order, set history and RNG");end_duel(h);


  recordHeader(configRec,true,&configSetup);configPoints.Reset(seed,configSetup,0,configRec);
  h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"v5 deck fixture initializes");get_message(h,configBuffer.data());start_duel(h,0);configPump();
  auto v5Apply=[&](editor_command c){c.version=5;c.revision=editor_revision(h);check(editor_apply(h,&c)==EDITOR_OK,"v5 deck command accepted");configPoints.Edit(c);configRec.WriteEditorCommand(c);configPump();};
  auto order=[&](int p){std::vector<uint64_t> ids;for(auto* c:reinterpret_cast<duel*>(h)->game_field->player[p].list_main)ids.push_back(c->cardid);return ids;};
  auto beforeDelete=order(0);auto otherDeck=order(1);f=reinterpret_cast<duel*>(h)->game_field;auto* removed=f->player[0].list_main[1];editor_command erase{};erase.opcode=3;erase.target_low=uint32_t(removed->cardid);erase.target_high=uint32_t(removed->cardid>>32);beforeDelete.erase(beforeDelete.begin()+1);v5Apply(erase);
  check(order(0)==beforeDelete && order(1)==otherDeck,"Deleting a middle deck card preserves every remaining card identity and order after engine processing");f=reinterpret_cast<duel*>(h)->game_field;check(!f->core.shuffle_deck_check[0] && !f->core.shuffle_deck_check[1],"Editor deletion does not schedule automatic shuffling");
  auto deletionState=EditorHistory::Snapshot(h);check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==deletionState,"Ordered deletion undo and forward reproduce exact engine state and RNG");
  editor_command shuffle{};shuffle.opcode=7;shuffle.player=1;shuffle.location=LOCATION_DECK;v5Apply(shuffle);auto shuffled=order(1);auto sortedBefore=otherDeck,sortedAfter=shuffled;std::sort(sortedBefore.begin(),sortedBefore.end());std::sort(sortedAfter.begin(),sortedAfter.end());check(sortedBefore==sortedAfter && shuffled!=otherDeck && order(0)==beforeDelete,"Explicit shuffle permutes only the selected player's deck without changing cards");
  auto shuffleState=EditorHistory::Snapshot(h);check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==shuffleState,"Explicit shuffle undo and forward preserve exact shuffled order and RNG");check(configPoints.Save("../../work/v5-deck.yges","v5 ordered deletion and shuffle") && configPoints.Load("../../work/v5-deck.yges",h,configRec) && EditorHistory::Snapshot(h)==shuffleState,"Explicit shuffle save and load restore exact order and RNG");
  configRec.EndRecord();check(configRec.SaveReplay(L"v5-deck-smoke"),"v5 ordered deletion and shuffle replay saves");end_duel(h);Replay deckV5;check(deckV5.OpenReplay(L"v5-deck-smoke.yrp"),"v5 replay opens");deckV5.SkipInfo();h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"v5 replay initial decks reconstruct");get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);int appliedV5=0;while(deckV5.ReadNextInput(resp.data(),saved,isEdit)){if(isEdit){check(saved.version==5 && editor_apply(h,&saved)==EDITOR_OK,"Actual replay executes v5 deck command");++appliedV5;}else set_responseb(h,resp.data());toIdle(h);}check(appliedV5==2 && EditorHistory::Snapshot(h)==shuffleState,"Actual replay reproduces ordered deletion and explicit shuffled deck with identical RNG");end_duel(h);

  uint32_t plainPend=0,extraPend=0,fusionCode=0;for(const auto& row:dataManager.GetDataTable()){uint32_t type=row.second.type;if(!(type&TYPE_MONSTER))continue;if((type&TYPE_PENDULUM) && !(type&TYPES_EXTRA_DECK) && !plainPend)plainPend=row.first;if((type&TYPE_PENDULUM) && (type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ)) && !extraPend)extraPend=row.first;if((type&TYPE_FUSION) && !(type&TYPE_PENDULUM) && !fusionCode)fusionCode=row.first;}
  check(plainPend && extraPend && fusionCode,"Database has ordinary and extra-category Pendulum monsters for actual tests");EditorSetup regionSetup=configSetup;regionSetup.decks[0].extra.assign(3,fusionCode);recordHeader(configRec,true,&regionSetup);configPoints.Reset(seed,regionSetup,0,configRec);h=create_duel_v2(seed);editor_enable(h,1);check(regionSetup.Initialize(h),"v6 region fixture initializes");get_message(h,configBuffer.data());start_duel(h,0);configPump();
  auto v6Apply=[&](editor_command c){c.version=6;c.revision=editor_revision(h);check(editor_apply(h,&c)==EDITOR_OK,"v6 region edit accepted");configPoints.Edit(c);configRec.WriteEditorCommand(c);configPump();};auto v6Reject=[&](editor_command c,const char* label){c.revision=editor_revision(h);auto state=EditorHistory::Snapshot(h);check(editor_apply(h,&c)!=EDITOR_OK && EditorHistory::Snapshot(h)==state,label);};
  editor_command grave{};grave.opcode=1;grave.code=89631139;grave.location=LOCATION_GRAVE;grave.value=EDITOR_SENT_GRAVE_TURN;v6Apply(grave);f=reinterpret_cast<duel*>(h)->game_field;auto* graveCard=f->player[0].list_grave.back();check(graveCard->turnid==f->infos.turn_id,"Grave history uses the real arrival turn read by card effects");auto* state=reinterpret_cast<duel*>(h)->lua->current_state;check(luaL_dostring(state,"return Duel.GetFieldCard(0,16,0):GetTurnID()==Duel.GetTurnCount()")==0 && lua_toboolean(state,-1),"Real Lua GetTurnID identifies editor-generated card as sent this turn");lua_pop(state,1);
  grave.opcode=4;grave.target_low=uint32_t(graveCard->cardid);grave.target_high=uint32_t(graveCard->cardid>>32);grave.value=0;v6Apply(grave);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_grave.back()->turnid!=f->infos.turn_id,"Existing grave card can be marked as sent on a previous turn");
  auto previousGrave=EditorHistory::Snapshot(h);check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==previousGrave,"Grave turn history survives undo and forward exactly");
  auto extraIDs=[&](){std::vector<uint64_t> ids;for(auto* c:reinterpret_cast<duel*>(h)->game_field->player[0].list_extra)ids.push_back(c->cardid);return ids;};auto initialExtra=extraIDs();editor_command extraInsert{};extraInsert.opcode=1;extraInsert.code=fusionCode;extraInsert.location=LOCATION_EXTRA;extraInsert.position=POS_FACEDOWN_DEFENSE;v6Apply(extraInsert);auto afterTop=extraIDs();auto extraTopID=afterTop.front();afterTop.erase(afterTop.begin());check(afterTop==initialExtra,"Extra top insertion preserves every existing card order");extraInsert.sequence=1;v6Apply(extraInsert);auto afterBottom=extraIDs();auto extraBottomID=afterBottom.back();check(afterBottom.front()==extraTopID && std::equal(initialExtra.begin(),initialExtra.end(),afterBottom.begin()+1),"Extra bottom insertion preserves existing order");extraInsert.sequence=2;auto beforeMiddle=extraIDs();v6Apply(extraInsert);auto afterMiddle=extraIDs();std::vector<uint64_t> oldExtra;size_t newIndex=0;for(size_t i=0;i<afterMiddle.size();++i)if(std::find(beforeMiddle.begin(),beforeMiddle.end(),afterMiddle[i])!=beforeMiddle.end())oldExtra.push_back(afterMiddle[i]);else newIndex=i;check(oldExtra==beforeMiddle && newIndex>0 && newIndex+1<afterMiddle.size(),"Extra random option inserts strictly between existing cards without reordering them");
  extraInsert.code=plainPend;extraInsert.sequence=0;v6Reject(extraInsert,"Ordinary Pendulum cannot exist facedown in Extra Deck");extraInsert.position=POS_FACEUP_ATTACK;v6Apply(extraInsert);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].extra_p_count==1 && f->player[0].list_extra.back()->data.code==plainPend,"Ordinary Pendulum inserts into face-up Extra group");extraInsert.code=extraPend;extraInsert.position=POS_FACEDOWN_DEFENSE;v6Apply(extraInsert);extraInsert.position=POS_FACEUP_ATTACK;extraInsert.sequence=1;v6Apply(extraInsert);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].extra_p_count==2 && f->player[0].list_extra.back()->data.code==extraPend,"Fusion Synchro or Xyz Pendulum supports both Extra positions in their separate groups");
  extraInsert.code=fusionCode;v6Reject(extraInsert,"Non-Pendulum Extra monster cannot be placed face-up");editor_command pzone{};pzone.opcode=1;pzone.code=89631139;pzone.location=LOCATION_PZONE;v6Reject(pzone,"Non-Pendulum monster cannot enter Pendulum zone");pzone.code=plainPend;v6Apply(pzone);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_szone[f->get_pzone_sequence(0)]->current.pzone,"Pendulum monster enters its actual native Pendulum zone");
  auto regionFinal=EditorHistory::Snapshot(h);check(configPoints.Save("../../work/v6-regions.yges","v6 grave and extra insertion") && configPoints.Load("../../work/v6-regions.yges",h,configRec) && EditorHistory::Snapshot(h)==regionFinal,"Save and load preserve grave history, Extra groups, exact order and RNG");configRec.EndRecord();check(configRec.SaveReplay(L"v6-regions-smoke"),"v6 region replay saves");end_duel(h);Replay regionReplay;check(regionReplay.OpenReplay(L"v6-regions-smoke.yrp"),"v6 replay opens");regionReplay.SkipInfo();h=create_duel_v2(seed);editor_enable(h,1);check(regionSetup.Initialize(h),"v6 replay initial decks reconstruct");get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);int appliedV6=0;while(regionReplay.ReadNextInput(resp.data(),saved,isEdit)){if(isEdit){check(saved.version==6 && editor_apply(h,&saved)==EDITOR_OK,"Actual replay executes v6 grave and Extra insertion edit");++appliedV6;}else set_responseb(h,resp.data());toIdle(h);}check(appliedV6==9 && EditorHistory::Snapshot(h)==regionFinal,"Actual replay reproduces grave history, Extra insertion order, groups and RNG");end_duel(h);

  EditorSetup originSetup=configSetup;recordHeader(configRec,true,&originSetup);configPoints.Reset(seed,originSetup,0,configRec);h=create_duel_v2(seed);editor_enable(h,1);check(originSetup.Initialize(h),"v7 summon origin fixture initializes");get_message(h,configBuffer.data());start_duel(h,0);configPump();
  auto originApply=[&](editor_command c){c.version=7;c.revision=editor_revision(h);check(editor_apply(h,&c)==EDITOR_OK,"v7 summon method edit accepted");configPoints.Edit(c);configRec.WriteEditorCommand(c);configPump();};auto originReject=[&](editor_command c,const char* label){c.revision=editor_revision(h);auto state=EditorHistory::Snapshot(h);check(editor_apply(h,&c)!=EDITOR_OK && EditorHistory::Snapshot(h)==state,label);};
  editor_command origin{};origin.opcode=1;origin.code=fusionCode;origin.location=LOCATION_MZONE;origin.sequence=5;origin.value=EDITOR_SPECIAL_ORIGIN|EDITOR_SUMMON_SPECIAL|(LOCATION_EXTRA<<8);originApply(origin);f=reinterpret_cast<duel*>(h)->game_field;auto* fusionOrigin=f->player[0].list_mzone[5];check(fusionOrigin && ((fusionOrigin->summon_info>>16)&0xff)==LOCATION_EXTRA && !fusionOrigin->is_status(STATUS_PROC_COMPLETE),"Fusion can be special summoned from Extra into EMZ without regular completion");auto* originLua=reinterpret_cast<duel*>(h)->lua->current_state;check(luaL_dostring(originLua,"local c=Duel.GetFieldCard(0,4,5);return c:GetSummonLocation()==64 and c:IsSummonLocation(64)")==0 && lua_toboolean(originLua,-1),"Real Lua summon-location predicates see configured Extra source");lua_pop(originLua,1);
  origin.opcode=4;origin.target_low=uint32_t(fusionOrigin->cardid);origin.target_high=uint32_t(fusionOrigin->cardid>>32);origin.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_GRAVE<<8);originReject(origin,"Grave-source monster cannot remain in Extra Monster Zone");origin.sequence=0;origin.value|=EDITOR_PROC_COMPLETE;originApply(origin);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_mzone[0]->is_status(STATUS_PROC_COMPLETE) && ((f->player[0].list_mzone[0]->summon_info>>16)&0xff)==LOCATION_GRAVE,"Regular completion and special summon source are independently configurable");origin.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_GRAVE<<8);originApply(origin);f=reinterpret_cast<duel*>(h)->game_field;check(!f->player[0].list_mzone[0]->is_status(STATUS_PROC_COMPLETE) && !f->player[0].list_mzone[0]->is_status(STATUS_SPSUMMON_TURN) && ((f->player[0].list_mzone[0]->summon_info>>16)&0xff)==LOCATION_GRAVE,"Special method and source remain set with both regular completion and this-turn history disabled");
  editor_command ordinary{};ordinary.opcode=1;ordinary.code=89631139;ordinary.location=LOCATION_MZONE;ordinary.sequence=1;ordinary.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<8);originReject(ordinary,"Ordinary monster cannot select Extra Deck special-summon source");ordinary.value=EDITOR_NORMAL_ORIGIN|EDITOR_SUMMON_NORMAL;originApply(ordinary);f=reinterpret_cast<duel*>(h)->game_field;auto* normalCard=f->player[0].list_mzone[1];auto normalID=normalCard->cardid;check((normalCard->summon_info&SUMMON_TYPE_NORMAL) && ((normalCard->summon_info>>16)&0xff)==LOCATION_HAND,"Normal method writes real normal summon type with mandatory hand source");ordinary.opcode=4;ordinary.target_low=uint32_t(normalID);ordinary.target_high=uint32_t(normalID>>32);ordinary.player=1;ordinary.sequence=0;originApply(ordinary);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[1].list_mzone[0]->summon_player==0 && f->player[1].list_mzone[0]->owner==0,"Editing control preserves historical summon player when method and source are unchanged");
  ordinary=editor_command{};ordinary.opcode=1;ordinary.code=89631139;ordinary.location=LOCATION_MZONE;ordinary.sequence=2;ordinary.value=EDITOR_FLIP_ORIGIN|EDITOR_SUMMON_FLIP;originApply(ordinary);f=reinterpret_cast<duel*>(h)->game_field;check((f->player[0].list_mzone[2]->summon_info&SUMMON_TYPE_FLIP) && ((f->player[0].list_mzone[2]->summon_info>>16)&0xff)==LOCATION_MZONE && f->player[0].list_mzone[2]->is_status(STATUS_FLIP_SUMMON_TURN),"Flip method has real flip type, fixed field source and this-turn flag");ordinary.sequence=3;ordinary.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_GRAVE<<8);originApply(ordinary);f=reinterpret_cast<duel*>(h)->game_field;check(((f->player[0].list_mzone[3]->summon_info>>16)&0xff)==LOCATION_GRAVE && !f->player[0].list_mzone[3]->is_status(STATUS_SPSUMMON_TURN),"Ordinary monster supports non-Extra special source independently of this-turn history");
  ordinary.sequence=1;ordinary.value=EDITOR_NORMAL_ORIGIN|EDITOR_SPECIAL_ORIGIN|(LOCATION_HAND<<8);originReject(ordinary,"Normal and special methods reject simultaneous selection");ordinary.value=EDITOR_NORMAL_ORIGIN|(LOCATION_GRAVE<<8);originReject(ordinary,"Normal summon rejects non-hand source");ordinary.value=EDITOR_FLIP_ORIGIN|(LOCATION_HAND<<8);originReject(ordinary,"Flip summon rejects non-field source");ordinary.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_GRAVE<<8);ordinary.sequence=5;originReject(ordinary,"Non-Extra source cannot enter empty Extra Monster Zone");
  uint32_t linkOriginCode=0;for(const auto& row:dataManager.GetDataTable())if(row.second.type&TYPE_LINK){linkOriginCode=row.first;break;}ordinary.code=linkOriginCode;ordinary.sequence=4;ordinary.value=EDITOR_FLIP_ORIGIN;originReject(ordinary,"Link monster cannot be assigned flip summon method");ordinary.value=EDITOR_NORMAL_ORIGIN;originReject(ordinary,"Link monster cannot be assigned normal summon method");ordinary.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<8);originApply(ordinary);ordinary.code=plainPend;ordinary.sequence=6;originApply(ordinary);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_mzone[6] && f->player[0].list_mzone[6]->data.code==plainPend,"Extra-source Pendulum enters an actual Extra Monster Zone");
  editor_command ownTrap{};ownTrap.opcode=1;ownTrap.code=13955608;ownTrap.form=4;ownTrap.self_summon=1;ownTrap.player=1;ownTrap.owner=1;ownTrap.location=LOCATION_MZONE;ownTrap.sequence=1;originApply(ownTrap);f=reinterpret_cast<duel*>(h)->game_field;auto* ownTrapCard=f->player[1].list_mzone[1];check(ownTrapCard && (ownTrapCard->summon_info&0xff00ffff)==(SUMMON_TYPE_SPECIAL|1) && ((ownTrapCard->summon_info>>16)&0xff)==LOCATION_SZONE && !ownTrapCard->is_status(STATUS_SPSUMMON_TURN),"Native own-effect trap preserves its real self summon value and back-row source with independent turn history");
  ownTrap.opcode=4;ownTrap.target_low=uint32_t(ownTrapCard->cardid);ownTrap.target_high=uint32_t(ownTrapCard->cardid>>32);ownTrap.self_summon=0;ownTrap.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_SZONE<<8);originApply(ownTrap);f=reinterpret_cast<duel*>(h)->game_field;check((f->player[1].list_mzone[1]->summon_info&0xff00ffff)==SUMMON_TYPE_SPECIAL && !f->player[1].list_mzone[1]->current.reason_effect,"Disabling native own-effect provenance removes self summon value without losing special method");ownTrap.self_summon=1;originApply(ownTrap);f=reinterpret_cast<duel*>(h)->game_field;check((f->player[1].list_mzone[1]->summon_info&0xff00ffff)==(SUMMON_TYPE_SPECIAL|1) && f->player[1].list_mzone[1]->current.reason_effect,"Re-enabling native own-effect provenance restores the real self summon value");
  origin.sequence=0;origin.value=EDITOR_NORMAL_ORIGIN;originReject(origin,"Fusion monster rejects normal method regardless of regular completion checkbox");auto originFinal=EditorHistory::Snapshot(h);check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==originFinal,"Summon method and source reconstruct through undo and forward");check(configPoints.Save("../../work/v7-summon-origin.yges","v7 summon origin") && configPoints.Load("../../work/v7-summon-origin.yges",h,configRec) && EditorHistory::Snapshot(h)==originFinal,"Save and load preserve methods, fixed sources and EMZ placement");configRec.EndRecord();check(configRec.SaveReplay(L"v7-summon-origin-smoke"),"v7 summon origin replay saves");end_duel(h);Replay originReplay;check(originReplay.OpenReplay(L"v7-summon-origin-smoke.yrp"),"v7 replay opens");originReplay.SkipInfo();h=create_duel_v2(seed);editor_enable(h,1);check(originSetup.Initialize(h),"v7 replay initial decks reconstruct");get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);int appliedV7=0;while(originReplay.ReadNextInput(resp.data(),saved,isEdit)){if(isEdit){check(saved.version==7 && editor_apply(h,&saved)==EDITOR_OK,"Actual replay executes v7 summon origin edit");++appliedV7;}else set_responseb(h,resp.data());toIdle(h);}check(appliedV7==12 && EditorHistory::Snapshot(h)==originFinal,"Actual replay reproduces methods, grave and Extra sources, EMZs and RNG");end_duel(h);

  h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"v8 trap placement fixture initializes");get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);
  editor_command limitedTrap{};limitedTrap.opcode=1;limitedTrap.code=13955608;limitedTrap.form=4;limitedTrap.location=LOCATION_MZONE;limitedTrap.self_summon=1;limitedTrap.sequence=5;auto trapReject=[&](editor_command c,const char* label){c.revision=editor_revision(h);auto before=EditorHistory::Snapshot(h);check(editor_apply(h,&c)!=EDITOR_OK && EditorHistory::Snapshot(h)==before,label);};trapReject(limitedTrap,"Native trap monster rejects slot six without mutating engine state");limitedTrap.sequence=6;trapReject(limitedTrap,"Native trap monster rejects slot seven");limitedTrap.sequence=0;limitedTrap.location=LOCATION_GRAVE;trapReject(limitedTrap,"Monster-form trap cannot be generated outside monster zone");limitedTrap.location=LOCATION_MZONE;limitedTrap.form=6;limitedTrap.self_summon=0;limitedTrap.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_FUSION;limitedTrap.race=RACE_MACHINE;limitedTrap.attribute=ATTRIBUTE_EARTH;limitedTrap.level=4;limitedTrap.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<8);trapReject(limitedTrap,"Custom fusion subtype cannot give a trap monster Extra Deck source or EMZ access");limitedTrap.value=EDITOR_NORMAL_ORIGIN;trapReject(limitedTrap,"Monster-form trap cannot claim normal summon origin");limitedTrap.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_SZONE<<8);limitedTrap.revision=editor_revision(h);check(editor_apply(h,&limitedTrap)==EDITOR_OK,"Custom trap monster remains valid in a Main Monster Zone with back-row source");toIdle(h);limitedTrap.sequence=1;limitedTrap.monster_flags=TYPE_MONSTER|TYPE_NORMAL;limitedTrap.value=EDITOR_SUMMON_NORMAL;trapReject(limitedTrap,"Trap monster rejects legacy normal-summon history even without explicit method flag");end_duel(h);

  recordHeader(configRec,true,&configSetup);configPoints.Reset(seed,configSetup,0,configRec);h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"Link-arrow fixture initializes");get_message(h,configBuffer.data());start_duel(h,0);configPump();
  editor_command arrows{};arrows.opcode=1;arrows.code=89631139;arrows.location=LOCATION_MZONE;arrows.position=POS_FACEUP_ATTACK;arrows.form=5;arrows.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_LINK;arrows.race=RACE_DRAGON;arrows.attribute=ATTRIBUTE_LIGHT;arrows.attack=2500;arrows.level=2;arrows.defense=LINK_MARKER_TOP_RIGHT|LINK_MARKER_BOTTOM_LEFT;arrows.revision=editor_revision(h);check(editor_apply(h,&arrows)==EDITOR_OK,"Custom Link arrows accepted by actual core");configPoints.Edit(arrows);configRec.WriteEditorCommand(arrows);configPump();f=reinterpret_cast<duel*>(h)->game_field;auto* linked=f->player[0].list_mzone[0];check(linked->get_link_marker()==arrows.defense && linked->get_link()==2 && linked->is_link_marker(LINK_MARKER_TOP_RIGHT),"Link marker getter and Link rating reflect chosen arrows");
  auto arrowState=EditorHistory::Snapshot(h);check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==arrowState,"Undo and forward reconstruct custom Link arrows");check(configPoints.Save("../../work/v9-arrows.yges","v9 link arrows") && configPoints.Load("../../work/v9-arrows.yges",h,configRec) && EditorHistory::Snapshot(h)==arrowState,"Save and load preserve Link arrows");editor_command badArrows=arrows;badArrows.sequence=1;badArrows.defense=0x10;badArrows.revision=editor_revision(h);check(editor_apply(h,&badArrows)==EDITOR_BAD_COMMAND && EditorHistory::Snapshot(h)==arrowState,"Unavailable center arrow rejected without mutation");configRec.EndRecord();check(configRec.SaveReplay(L"v9-arrow-smoke"),"Custom Link arrows save in real replay");end_duel(h);Replay arrowReplay;check(arrowReplay.OpenReplay(L"v9-arrow-smoke.yrp"),"Custom arrow replay opens");arrowReplay.SkipInfo();h=create_duel_v2(seed);editor_enable(h,1);configSetup.Initialize(h);get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);int arrowRecords=0;while(arrowReplay.ReadNextInput(resp.data(),saved,isEdit)){if(isEdit){check(saved.version==9 && editor_apply(h,&saved)==EDITOR_OK,"Real replay applies v9 Link arrow metadata");++arrowRecords;}else set_responseb(h,resp.data());toIdle(h);}check(arrowRecords==1 && EditorHistory::Snapshot(h)==arrowState,"Real replay reproduces exact custom Link arrows and card state");end_duel(h);
  h=create_duel_v2(seed);editor_enable(h,1);configSetup.Initialize(h);get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);editor_command nonEffect{};nonEffect.opcode=1;nonEffect.code=89631139;nonEffect.location=LOCATION_MZONE;nonEffect.position=POS_FACEUP_ATTACK;nonEffect.form=5;nonEffect.monster_flags=TYPE_MONSTER|TYPE_FUSION;nonEffect.race=RACE_DRAGON;nonEffect.attribute=ATTRIBUTE_LIGHT;nonEffect.level=8;nonEffect.attack=3000;nonEffect.defense=2500;nonEffect.revision=editor_revision(h);check(editor_apply(h,&nonEffect)==EDITOR_OK,"Non-effect Fusion custom monster accepts neither Normal nor Effect type");toIdle(h);f=reinterpret_cast<duel*>(h)->game_field;check((f->player[0].list_mzone[0]->get_type()&(TYPE_NORMAL|TYPE_EFFECT))==0 && (f->player[0].list_mzone[0]->get_type()&TYPE_FUSION),"Non-effect Fusion is not a Normal Monster in actual card type");end_duel(h);
  recordHeader(configRec,true,&configSetup);configPoints.Reset(seed,configSetup,0,configRec);h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"Immediate draw fixture initializes");get_message(h,configBuffer.data());start_duel(h,0);configPump();f=reinterpret_cast<duel*>(h)->game_field;auto beforeDrawHand=f->player[1].list_hand.size(),beforeDrawDeck=f->player[1].list_main.size();std::vector<uint64_t> drawTop;for(size_t i=0;i<2;++i)drawTop.push_back(f->player[1].list_main[beforeDrawDeck-1-i]->cardid);editor_command draw{};draw.opcode=8;draw.player=1;draw.value=2;draw.revision=editor_revision(h);check(editor_apply(h,&draw)==EDITOR_OK,"Immediate draw command accepted for selected controller");configPoints.Edit(draw);configRec.WriteEditorCommand(draw);configPump();f=reinterpret_cast<duel*>(h)->game_field;check(f->player[1].list_hand.size()==beforeDrawHand+2 && f->player[1].list_main.size()==beforeDrawDeck-2 && f->player[1].list_hand[beforeDrawHand]->cardid==drawTop[0] && f->player[1].list_hand[beforeDrawHand+1]->cardid==drawTop[1],"Real draw transfers exact top cards in deck order to selected hand");auto drawState=EditorHistory::Snapshot(h);check(configPoints.Back(h,configRec) && configPoints.Forward(h,configRec) && EditorHistory::Snapshot(h)==drawState,"Undo and forward reproduce draw and exact card identities");check(configPoints.Save("../../work/v9-draw.yges","v9 draw") && configPoints.Load("../../work/v9-draw.yges",h,configRec) && EditorHistory::Snapshot(h)==drawState,"Save and load preserve editor draw");configRec.EndRecord();check(configRec.SaveReplay(L"v9-draw-smoke"),"Editor draw records in actual replay");end_duel(h);Replay drawReplay;check(drawReplay.OpenReplay(L"v9-draw-smoke.yrp"),"Draw replay opens");drawReplay.SkipInfo();h=create_duel_v2(seed);editor_enable(h,1);configSetup.Initialize(h);get_message(h,configBuffer.data());start_duel(h,0);toIdle(h);int drawRecords=0;while(drawReplay.ReadNextInput(resp.data(),saved,isEdit)){if(isEdit){check(saved.opcode==8 && editor_apply(h,&saved)==EDITOR_OK,"Actual replay executes editor draw");++drawRecords;}else set_responseb(h,resp.data());toIdle(h);}check(drawRecords==1 && EditorHistory::Snapshot(h)==drawState,"Actual replay reproduces draw and full card state");end_duel(h);

  {
   EditorSetup initial;initial.decks[0].main.assign(12,89631139);initial.decks[1].main.assign(12,89631139);initial.hand=3;initial.hand1=7;Replay recording;recordHeader(recording,true,&initial,6);EditorTimeline timeline;timeline.Reset(seed,initial,0,recording);h=create_duel_v2(seed);editor_enable(h,3);check(initial.Initialize(h),"New opening setup initializes with independent custom hand counts");std::vector<uint8_t> packet(SIZE_MESSAGE_BUFFER);get_message(h,packet.data());start_duel(h,0);timeline.InitialPoint(h);f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_hand.empty()&&f->player[1].list_hand.empty()&&editor_can_edit(h),"New first point is editable and paused before either starting hand exists");
   auto step=[&](){uint32_t result=process(h);get_message(h,packet.data());timeline.Processed(h,result,true);return packet[0];};for(int i=0;i<10&&reinterpret_cast<duel*>(h)->game_field->player[0].list_hand.empty();++i)step();f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_hand.size()==3&&f->player[1].list_hand.empty()&&f->infos.turn_id==0,"Player one opening deal occurs before turn one and before opponent deal");auto firstState=EditorHistory::Snapshot(h,true);check(timeline.Back(h,recording)&&reinterpret_cast<duel*>(h)->game_field->player[0].list_hand.empty(),"Rewinding opening deal restores empty hand");check(timeline.Forward(h,recording)&&EditorHistory::Snapshot(h,true)==firstState,"Forward restores exact opening hand and pending second deal");for(int i=0;i<10&&reinterpret_cast<duel*>(h)->game_field->player[1].list_hand.empty();++i)step();check(reinterpret_cast<duel*>(h)->game_field->player[1].list_hand.size()==7,"Second opening deal uses opponent independent hand count");
   // Return to initial editable point, then create one Xyz with actual attachments.
   while(timeline.CanBack())check(timeline.Back(h,recording),"Opening timeline step can rewind");editor_command xyz{};xyz.opcode=1;xyz.code=84013237;xyz.location=LOCATION_MZONE;xyz.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<16>>8);editor_command material{};material.opcode=9;material.code=89631139;material.owner=1;material.value=2;editor_command counter{};counter.opcode=10;counter.code=1;counter.value=3;std::vector<editor_command> group{xyz,material,counter};check(timeline.EditGroup(h,group)==EDITOR_OK,"Transactional YGO edit creates Xyz two actual material cards and three counters");editor_command marker{};marker.opcode=11;marker.value=uint32_t(group.size());recording.WriteEditorCommand(marker,timeline.ProcessCount());for(const auto& cmd:group)recording.WriteEditorCommand(cmd,timeline.ProcessCount());step();f=reinterpret_cast<duel*>(h)->game_field;check(f->player[0].list_mzone[0]&&f->player[0].list_mzone[0]->xyz_materials.size()==2&&f->player[0].list_mzone[0]->get_counter(1)==3,"Native Xyz material group and counter table contain configured state");check(f->player[0].list_mzone[0]->xyz_materials[0]->owner==1,"Material original owner survives generated attachment");auto complete=EditorHistory::Snapshot(h,true);size_t point=timeline.Number();timeline.Log(L"before saved point",84013237);auto bad=group;bad[0]=counter;bad[0].value=8;bad[0].target_low=uint32_t(f->player[0].list_mzone[0]->cardid);bad[1].code=0;check(timeline.EditGroup(h,bad)!=EDITOR_OK&&timeline.Number()==point&&EditorHistory::Snapshot(h,true)==complete,"Invalid group keeps original scene counters random state and undo history");check(timeline.Back(h,recording)&&reinterpret_cast<duel*>(h)->game_field->player[0].list_mzone[0]==nullptr,"Single rewind removes generated host and every attachment");check(timeline.Forward(h,recording)&&EditorHistory::Snapshot(h,true)==complete,"Forward reproduces material IDs counter types and counts");check(timeline.Save("../../work/v015-attachments.yges","new opening attachments"),"New archive writes effect history alongside full grouped timeline");EditorTimeline loaded;check(loaded.Load("../../work/v015-attachments.yges",h,recording)&&EditorHistory::Snapshot(h,true)==complete,"Fresh timeline reads complete grouped state across sessions");check(loaded.Back(h,recording)&&loaded.Forward(h,recording)&&EditorHistory::Snapshot(h,true)==complete,"Loaded grouped save retains rewind and forward");recording.pheader.value2=timeline.ProcessCount();recording.pheader.value3=1;recording.EndRecord();check(recording.SaveReplay(L"v015-attachments"),"Version six replay saves opening and grouped attachments");end_duel(h);
   Replay replay;check(replay.OpenReplay(L"v015-attachments.yrp")&&replay.pheader.value1==6,"Version six recording parses while old replay tests still pass");replay.SkipInfo();h=create_duel_v2(seed);editor_enable(h,3);initial.Initialize(h);get_message(h,packet.data());start_duel(h,0);unsigned char answer[SIZE_RETURN_VALUE]{};editor_command command{};bool edit=false;check(replay.ReadNextInput(answer,command,edit)&&edit&&command.opcode==11&&replay.ApplyEditorInput(h,command)==EDITOR_OK,"Actual replay parser consumes transaction marker and material/counter members");process(h);get_message(h,packet.data());check(EditorHistory::Snapshot(h,true)==complete,"Real grouped replay reproduces exact saved native state");end_duel(h);
  }

  {
   h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"Position-rule fixture initializes");start_duel(h,0);toIdle(h);
   editor_command shape{};shape.opcode=1;shape.code=89631139;shape.location=LOCATION_MZONE;shape.position=POS_FACEUP_ATTACK;shape.form=5;shape.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_LINK;shape.race=RACE_DRAGON;shape.attribute=ATTRIBUTE_LIGHT;shape.attack=2500;shape.defense=LINK_MARKER_BOTTOM;shape.level=1;shape.revision=editor_revision(h);check(editor_apply(h,&shape)==EDITOR_OK,"Ordinary printed monster becomes a custom Link");toIdle(h);
   auto* custom=reinterpret_cast<duel*>(h)->game_field->player[0].list_mzone[0];check(!custom->is_capable_change_position(0),"Custom Link has no manual position-change command");check(!custom->is_capable_change_position_by_effect(0)&&!custom->is_capable_turn_set(0),"Custom Link rejects effect position changes and face-down setting");check(custom->get_spsummonable_position(nullptr,SUMMON_TYPE_SPECIAL,POS_FACEUP_ATTACK,0,0)==POS_FACEUP_ATTACK&&custom->get_spsummonable_position(nullptr,SUMMON_TYPE_SPECIAL,POS_FACEUP_DEFENSE|POS_FACEDOWN_DEFENSE,0,0)==0,"Custom Link only accepts face-up attack summon positions");
   auto* board=reinterpret_cast<duel*>(h)->game_field;board->change_position(custom,nullptr,0,POS_FACEUP_DEFENSE,0);toIdle(h);check(custom->current.position==POS_FACEUP_ATTACK,"Actual position-change processor cannot put custom Link in defense");board->change_position(custom,nullptr,0,POS_FACEDOWN_DEFENSE,0);toIdle(h);check(custom->current.position==POS_FACEUP_ATTACK,"Actual position-change processor cannot set custom Link face down");
   shape.opcode=4;shape.target_low=uint32_t(custom->cardid);shape.target_high=uint32_t(uint64_t(custom->cardid)>>32);shape.monster_flags=TYPE_MONSTER|TYPE_EFFECT;shape.defense=2000;shape.level=5;shape.revision=editor_revision(h);check(editor_apply(h,&shape)==EDITOR_OK,"Custom Link can return to ordinary monster form");toIdle(h);check(custom->is_capable_change_position(0)&&custom->is_capable_change_position_by_effect(0)&&custom->is_capable_turn_set(0),"Leaving Link form restores ordinary position-change rules");end_duel(h);
  }
  {
   h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"Gryphon Baronne link fixture initializes");start_duel(h,0);toIdle(h);
   auto place=[&](uint32_t code,uint32_t player,uint32_t sequence){editor_command cmd{};cmd.opcode=1;cmd.code=code;cmd.player=cmd.owner=player;cmd.sequence=sequence;cmd.location=LOCATION_MZONE;cmd.position=POS_FACEUP_ATTACK;cmd.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<8)|1u;cmd.revision=editor_revision(h);check(editor_apply(h,&cmd)==EDITOR_OK,"Gryphon or Baronne created with special summon history");toIdle(h);};
   place(65330383,1,4);place(84815190,0,1);auto* board=reinterpret_cast<duel*>(h)->game_field;auto* baronne=board->player[0].list_mzone[1];effect* destroy=nullptr;for(auto& item:baronne->field_effect)if(item.second->type&EFFECT_TYPE_IGNITION){destroy=item.second;break;}check(destroy!=nullptr&&!baronne->is_link_state()&&!destroy->is_action_check(0),"Real Gryphon Lua restriction blocks unlinked special-summoned Baronne");
   editor_command link{};link.opcode=1;link.code=89631139;link.location=LOCATION_MZONE;link.sequence=0;link.position=POS_FACEUP_ATTACK;link.form=5;link.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_LINK;link.race=RACE_DRAGON;link.attribute=ATTRIBUTE_LIGHT;link.attack=2500;link.defense=LINK_MARKER_RIGHT;link.level=1;link.revision=editor_revision(h);check(editor_apply(h,&link)==EDITOR_OK,"Originally non-Link monster receives right-pointing custom Link form");toIdle(h);auto* custom=board->player[0].list_mzone[0];check((custom->get_linked_zone()&(1u<<1))&&baronne->is_link_state(),"Custom Link arrow creates actual linked zone and Baronne link state");card_set linked;custom->get_linked_cards(&linked);check(linked.count(baronne)==1&&destroy->is_action_check(0),"Actual Gryphon Lua restriction allows linked Baronne destroy effect");
   link.opcode=4;link.target_low=uint32_t(custom->cardid);link.target_high=uint32_t(uint64_t(custom->cardid)>>32);link.defense=LINK_MARKER_LEFT;link.revision=editor_revision(h);check(editor_apply(h,&link)==EDITOR_OK,"Custom Link arrow can be redirected away from Baronne");toIdle(h);check(!baronne->is_link_state()&&!destroy->is_action_check(0),"Redirecting custom arrow restores actual Gryphon restriction");
   link.defense=LINK_MARKER_RIGHT;link.revision=editor_revision(h);check(editor_apply(h,&link)==EDITOR_OK,"Custom Link points back to Baronne");toIdle(h);int activate=-1;for(size_t i=0;i<board->core.select_chains.size();++i)if(board->core.select_chains[i].triggering_effect==destroy)activate=int(i);check(activate>=0,"Linked Baronne destroy is present in actual legal activation choices");response(h,5|(activate<<16));pump(h);check(board->core.units.front().type==PROCESSOR_SELECT_CARD,"Actual Baronne destroy opens target selection");int target=-1;for(size_t i=0;i<board->core.select_cards.size();++i)if(board->core.select_cards[i]->data.code==65330383)target=int(i);check(target>=0,"Opponent Gryphon is a legal destruction target");std::array<uint8_t,SIZE_RETURN_VALUE> answer{};answer[0]=1;answer[1]=uint8_t(target);responseBytes(h,answer.data(),2);toIdle(h);check(board->player[1].list_mzone[4]==nullptr&&std::any_of(board->player[1].list_grave.begin(),board->player[1].list_grave.end(),[](card* c){return c->data.code==65330383;}),"Actual Baronne Lua effect destroys opponent Gryphon while linked by custom monster");
   link.code=84815190;link.sequence=1;link.target_low=uint32_t(baronne->cardid);link.target_high=uint32_t(uint64_t(baronne->cardid)>>32);link.defense=LINK_MARKER_LEFT;link.revision=editor_revision(h);check(editor_apply(h,&link)==EDITOR_OK,"Second originally non-Link monster receives reciprocal custom arrow");toIdle(h);card_set mutual;custom->get_mutual_linked_cards(&mutual);check((custom->get_mutual_linked_zone()&(1u<<1))&&(baronne->get_mutual_linked_zone()&1u)&&mutual.count(baronne)==1,"Custom Link forms participate in actual mutual linked zones and groups");end_duel(h);
  }
  {
   h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"Real summon material fixture initializes");start_duel(h,0);toIdle(h);
   editor_command host{};host.opcode=1;host.code=37818794;host.location=LOCATION_MZONE;host.position=POS_FACEUP_ATTACK;host.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<8);editor_command begin{};begin.opcode=12;begin.value=1;editor_command dark{};dark.opcode=13;dark.code=46986414;dark.location=LOCATION_HAND;dark.position=POS_FACEDOWN_DEFENSE;dark.value=LOCATION_GRAVE;dark.level=POS_FACEUP_ATTACK;editor_command red=dark;red.code=74677422;red.location=LOCATION_MZONE;red.sequence=2;red.position=POS_FACEUP_ATTACK;editor_command finish{};finish.opcode=14;std::vector<editor_command> edits{host,begin,dark,red,finish};check(editor_apply_group(h,edits.data(),edits.size())==EDITOR_OK,"Actual Dragoon fusion material and summon initialization succeeds");toIdle(h);auto* dragon=reinterpret_cast<duel*>(h)->game_field->player[0].list_mzone[0];effect* limit=nullptr;for(auto& item:dragon->single_effect)if(item.second->code==(37818795|EFFECT_FLAG_EFFECT))limit=item.second;check(limit&&!limit->label.empty()&&limit->label[0]==2,"Actual Dragoon Lua counts two Normal fusion materials for destruction uses");check(dragon->material_cards.size()==2&&(dragon->summon_info&SUMMON_TYPE_FUSION)==SUMMON_TYPE_FUSION,"Real monster material group and exact Fusion summon type are queryable");for(auto* c:dragon->material_cards)check(c->current.location==LOCATION_GRAVE&&c->current.reason_card==dragon&&(c->current.reason&(REASON_FUSION|REASON_MATERIAL))==(REASON_FUSION|REASON_MATERIAL),"Fusion material retains destination and summoned monster relationship");
   begin.target_low=uint32_t(dragon->cardid);begin.target_high=uint32_t(uint64_t(dragon->cardid)>>32);dark.target_low=begin.target_low;dark.target_high=begin.target_high;red.target_low=begin.target_low;red.target_high=begin.target_high;finish.target_low=begin.target_low;finish.target_high=begin.target_high;red.code=65741786;edits={begin,dark,red,finish};check(editor_apply_group(h,edits.data(),edits.size())==EDITOR_OK,"Existing Dragoon can change fusion material composition");toIdle(h);limit=nullptr;for(auto& item:dragon->single_effect)if(item.second->code==(37818795|EFFECT_FLAG_EFFECT))limit=item.second;check(limit&&limit->label[0]==1,"Actual Dragoon destruction count updates from two Normal materials to one");dark=red;dark.sequence=3;edits={begin,dark,red,finish};check(editor_apply_group(h,edits.data(),edits.size())==EDITOR_OK,"Fusion history can contain no Normal materials");toIdle(h);limit=nullptr;for(auto& item:dragon->single_effect)if(item.second->code==(37818795|EFFECT_FLAG_EFFECT))limit=item.second;check(limit==nullptr,"Actual Dragoon gains no destruction-use flag when fusion material group has zero Normals");end_duel(h);
   h=create_duel_v2(seed);editor_enable(h,1);check(configSetup.Initialize(h),"I:P material inheritance fixture initializes");start_duel(h,0);toIdle(h);host.code=89631139;host.form=5;host.monster_flags=TYPE_MONSTER|TYPE_EFFECT|TYPE_LINK;host.race=RACE_DRAGON;host.attribute=ATTRIBUTE_LIGHT;host.attack=2000;host.defense=LINK_MARKER_LEFT|LINK_MARKER_RIGHT;host.level=2;begin.value=4;dark.code=65741786;dark.location=LOCATION_MZONE;dark.sequence=1;dark.position=POS_FACEUP_ATTACK;edits={host,begin,dark,finish};check(editor_apply_group(h,edits.data(),edits.size())==EDITOR_OK,"Link summon initializes actual I:P material effect");toIdle(h);auto* linked=reinterpret_cast<duel*>(h)->game_field->player[0].list_mzone[0];int inherited=0;for(auto& item:linked->single_effect)if(item.second->code==EFFECT_INDESTRUCTABLE_EFFECT&&item.second->owner&&item.second->owner->data.code==65741786)inherited++;check(inherited==1,"Actual I:P Lua grants summoned monster effect-destruction protection");editor_summon_material_info record{};check(editor_list_summon_materials(h,linked->cardid,&record,1)==1&&record.source_location==LOCATION_MZONE&&record.destination_location==LOCATION_GRAVE,"Material movement exposes exact recorded origin and destination");
   begin.target_low=uint32_t(linked->cardid);begin.target_high=uint32_t(uint64_t(linked->cardid)>>32);dark.target_low=begin.target_low;dark.target_high=begin.target_high;dark.states=uint32_t(record.card_id);dark.restrictions=uint32_t(record.card_id>>32);finish.target_low=begin.target_low;finish.target_high=begin.target_high;edits={begin,dark,finish};check(editor_apply_group(h,edits.data(),edits.size())==EDITOR_OK,"Existing summoned monster can reapply existing material history");toIdle(h);inherited=0;for(auto& item:linked->single_effect)if(item.second->code==EFFECT_INDESTRUCTABLE_EFFECT&&item.second->owner&&item.second->owner->data.code==65741786)inherited++;check(inherited==1,"Reapplying material history does not duplicate inherited I:P effects");end_duel(h);
  }
  check(scriptErrors==0,"No card script errors during recording or replay");
  std::cout<<"SUCCESS "<<assertions<<" assertions; "<<events<<" replay inputs"<<std::endl;
  return 0;
 } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
