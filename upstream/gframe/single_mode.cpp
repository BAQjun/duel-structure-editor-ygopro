#include <random>
#include <thread>
#include "single_mode.h"
#include "duel_editor.h"
#include "editor_history.h"
#include "editor_timeline.h"
#include "../ocgcore/duel.h"
#include "../ocgcore/field.h"
#include <array>
#include <mutex>
#include <cstring>
#include "client_card.h"
#include "duelclient.h"
#include "game.h"
#include "data_manager.h"
#include "../ocgcore/ocgapi.h"

namespace ygo {
namespace {
 std::mutex inputMutex;
 std::array<unsigned char,SIZE_RETURN_VALUE> pendingResponse{};
 unsigned int pendingLength=0;
 bool responsePending=false,editPending=false,undoPending=false,viewPending=false;std::vector<uint8_t> viewPrompt;bool viewPromptWaiting=false;
 EditorHistory editorHistory;
 EditorTimeline timeline;bool restoringEditorDisplay=false;
 std::atomic<unsigned int> stepBudget{0};
 bool booting=true,editReflow=false;
 std::atomic<bool> atBoundary{false};
 std::string saveFile,saveName,loadFile;
 struct EditorJump{};
 editor_command pendingEdit{};std::vector<editor_command> pendingGroup;
}


intptr_t SingleMode::pduel = 0;
std::atomic<bool> SingleMode::paused{true};
std::atomic<bool> SingleMode::is_closing{false};
std::atomic<bool> SingleMode::is_continuing{false};
Replay SingleMode::last_replay;
size_t SingleMode::last_replay_response_size = 0;

bool SingleMode::StartPlay(const std::string& startupSave) {
	{std::lock_guard<std::mutex> lock(inputMutex);responsePending=false;editPending=false;undoPending=false;saveFile.clear();loadFile=startupSave;}
 paused=true;stepBudget=0;booting=true;editReflow=false;atBoundary=false;
	mainGame->singleSignal.SetNoWait(false);
	std::thread(SinglePlayThread).detach();
	return true;
}
void SingleMode::StopPlay(bool is_exiting) {
	is_closing = is_exiting;
 if(is_exiting)mainGame->replaySignal.Set();
	is_continuing = false;
	mainGame->actionSignal.Set();
	mainGame->singleSignal.Set();
}
void SingleMode::SetResponse(unsigned char* resp, unsigned int len) {
 if(!resp || !len || len>SIZE_RETURN_VALUE)return;
 std::lock_guard<std::mutex> lock(inputMutex);
 if(responsePending || editPending || undoPending)return;
 pendingResponse.fill(0);std::memcpy(pendingResponse.data(),resp,len);
 pendingLength=len;responsePending=true;
}
bool SingleMode::QueueEditorCommand(const editor_command& command) {
 std::lock_guard<std::mutex> lock(inputMutex);
 if(!DuelEditor::active || editPending || undoPending)return false;
 responsePending=false;pendingGroup.clear();pendingEdit=command;editPending=true;
 mainGame->singleSignal.Set();return true;
}
bool SingleMode::QueueEditorGroup(const std::vector<editor_command>& commands){std::lock_guard<std::mutex> lock(inputMutex);if(!DuelEditor::active || editPending || undoPending || commands.empty() || commands.size()>512)return false;responsePending=false;pendingGroup=commands;pendingEdit=commands[0];editPending=true;mainGame->singleSignal.Set();return true;}
void SingleMode::RecordEditorLog(const wchar_t* text,int param){if(DuelEditor::active&&!restoringEditorDisplay)timeline.Log(text,param);}
bool SingleMode::QueueUndo() {
 std::lock_guard<std::mutex> lock(inputMutex);
 if(!DuelEditor::active || editPending || undoPending)return false;
 responsePending=false;paused=true;stepBudget=0;undoPending=true;mainGame->actionSignal.Set();mainGame->singleSignal.Set();return true;
}
void SingleMode::TogglePause(){paused=!paused.load();if(paused)stepBudget=0;mainGame->actionSignal.Set();mainGame->singleSignal.Set();}
void SingleMode::QueueViewSwitch(){std::lock_guard<std::mutex> lock(inputMutex);if(!DuelEditor::active)return;viewPending=!viewPending;mainGame->singleSignal.Set();mainGame->actionSignal.Set();}
void SingleMode::Step(){paused=true;stepBudget=1;mainGame->actionSignal.Set();mainGame->singleSignal.Set();}
bool SingleMode::QueueSave(const std::string& file,const std::string& name){std::lock_guard<std::mutex> lock(inputMutex);if(!DuelEditor::active || !saveFile.empty() || !loadFile.empty())return false;paused=true;stepBudget=0;saveFile=file;saveName=name;mainGame->actionSignal.Set();mainGame->singleSignal.Set();return true;}
bool SingleMode::QueueLoad(const std::string& file){std::lock_guard<std::mutex> lock(inputMutex);if(!DuelEditor::active || editPending || !loadFile.empty())return false;responsePending=false;paused=true;stepBudget=0;loadFile=file;mainGame->actionSignal.Set();mainGame->singleSignal.Set();return true;}
void SingleMode::RestoreView(){
 auto* f=reinterpret_cast<duel*>(pduel)->game_field;
 {std::lock_guard<std::mutex> lock(mainGame->gMutex);mainGame->HideElement(mainGame->wCardSelect);mainGame->HideElement(mainGame->wOptions);mainGame->HideElement(mainGame->wQuery);mainGame->HideElement(mainGame->wPosSelect);mainGame->dInfo.turn=f->infos.turn_id;mainGame->dInfo.curMsg=0;mainGame->dInfo.isStarted=true;mainGame->dInfo.isInDuel=true;mainGame->dInfo.isSingleMode=true;mainGame->device->setEventReceiver(&mainGame->dField);}
 restoringEditorDisplay=true;
 f->reload_field_info();std::vector<uint8_t> buf(SIZE_MESSAGE_BUFFER);auto len=get_message(pduel,buf.data());DuelClient::ClientAnalyze(buf.data(),len);SinglePlayRefresh();
 unsigned char phase[]={MSG_NEW_PHASE,uint8_t(f->infos.phase),uint8_t(f->infos.phase>>8)};if(f->infos.phase)DuelClient::ClientAnalyze(phase,sizeof phase);restoringEditorDisplay=false;
}
bool SingleMode::ServiceActions(bool waiting){
 std::vector<editor_command> group;editor_command command{};bool edit=false,undo=false,response=false,view=false;std::array<uint8_t,SIZE_RETURN_VALUE> resp{};unsigned int length=0;std::string save,name,load;
 {std::lock_guard<std::mutex> lock(inputMutex);edit=editPending;undo=undoPending;command=pendingEdit;group.swap(pendingGroup);save.swap(saveFile);name.swap(saveName);load.swap(loadFile);editPending=false;undoPending=false;view=viewPending;viewPending=false;
  if(waiting && responsePending && (!paused || stepBudget)){response=true;resp=pendingResponse;length=pendingLength;responsePending=false;}}
 if(view){DuelEditor::ApplyViewSwitch();if(viewPromptWaiting && !viewPrompt.empty()){DuelClient::ClientAnalyze(viewPrompt.data(),viewPrompt.size());DuelEditor::MaskPendingPrompt();}RefreshEditorState();}
 if(!load.empty() || undo || (stepBudget && timeline.CanForward() && !response)){
  paused=true;stepBudget=0;bool restored=!load.empty()?timeline.Load(load,pduel,last_replay):undo?timeline.Back(pduel,last_replay):timeline.Forward(pduel,last_replay);
  if(restored){viewPromptWaiting=false;viewPrompt.clear();DuelEditor::SetPromptPlayer(2);last_replay_response_size=0;DuelEditor::Available(false,editor_revision(pduel));timeline.RestoreLogs();RestoreView();timeline.RestoreLogs();DuelEditor::SaveResult(!load.empty()?L"存档已读取":L"已切换时点");throw EditorJump{};}
  DuelEditor::SaveResult(L"时点恢复失败，当前局面保留");RefreshEditorState();return false;
 }
 if(!save.empty()){char clock[32]{};std::time_t hkTime=std::time(nullptr)+8*3600;std::strftime(clock,sizeof clock,"%m-%d %H:%M:%S",std::gmtime(&hkTime));name+=" · "+std::string(clock)+" · 时点 "+std::to_string(timeline.Number());bool ok=timeline.Save(save,name);DuelEditor::SaveResult(ok?L"当前时点已保存":L"无法保存当前时点");}
 if(edit){std::vector<editor_card_info> before(editor_list_cards(pduel,nullptr,0));editor_list_cards(pduel,before.data(),uint32_t(before.size()));bool room=last_replay.CanWriteEditorCommand() && last_replay.CaptureRecording().size()+(group.size()+1)*(sizeof(editor_command)+6)<MAX_REPLAY_SIZE;int result=room?(group.empty()?editor_apply(pduel,&command):timeline.EditGroup(pduel,group)):8;
  if(result==EDITOR_OK){if(group.empty()){last_replay.WriteEditorCommand(command,timeline.ProcessCount());timeline.Edit(command);}else{editor_command marker{};marker.opcode=11;marker.value=uint32_t(group.size());last_replay.WriteEditorCommand(marker,timeline.ProcessCount());for(const auto& c:group)last_replay.WriteEditorCommand(c,timeline.ProcessCount());}last_replay_response_size=0;const auto& units=reinterpret_cast<duel*>(pduel)->game_field->core.units;editReflow=command.opcode==8 || (!units.empty() && (units.front().type==PROCESSOR_IDLE_COMMAND || units.front().type==PROCESSOR_BATTLE_COMMAND || units.front().type==PROCESSOR_PHASE_EVENT));paused=true;stepBudget=0;}
  if(result==EDITOR_OK){
   std::vector<editor_card_info> after(editor_list_cards(pduel,nullptr,0));editor_list_cards(pduel,after.data(),uint32_t(after.size()));
   auto record=[&](const editor_command& c){
    uint64_t id=uint64_t(c.target_low)|(uint64_t(c.target_high)<<32);const editor_card_info* target=nullptr;
    for(const auto& card:after)if(card.id==id){target=&card;break;}
    if(!target)for(const auto& card:before)if(card.id==id){target=&card;break;}
    uint32_t player=target?target->player:c.player;uint32_t code=target?target->code:c.code;
    std::wstring name=L"「"+std::wstring(dataManager.GetName(code))+L"」";
    std::wstring text=L"【编辑器】玩家 "+std::to_wstring(player+1)+L"：";
    if(c.opcode==1){const wchar_t* zone=c.location==LOCATION_MZONE?L"怪兽区":c.location==LOCATION_SZONE?L"魔法陷阱区":c.location==LOCATION_HAND?L"手牌":c.location==LOCATION_DECK?L"卡组":c.location==LOCATION_EXTRA?L"额外卡组":c.location==LOCATION_GRAVE?L"墓地":L"除外区";text+=std::wstring(L"在")+zone+L"生成"+name;}
    else if(c.opcode==2)text+=L"LP 设为 "+std::to_wstring(c.value);
    else if(c.opcode==8)text+=L"抽 "+std::to_wstring(c.value)+L" 张卡";
    else if(c.opcode==9)text+=L"为"+name+L"添加超量素材「"+std::wstring(dataManager.GetName(c.code))+L"」×"+std::to_wstring(c.value);
    else if(c.opcode==10)text+=L"将"+name+L"的指示物「"+std::wstring(dataManager.GetCounterName(c.code))+L"」数量设为 "+std::to_wstring(c.value);
    else if(c.opcode==3)text+=L"删除"+name;
    else if(c.opcode==4)text+=L"修改"+name+L"的形态、位置或状态";
    else if(c.opcode==5)text+=L"调整"+name+L"的牌组顺序";
    else if(c.opcode==7)text+=L"立即洗牌";
    else text+=L"修改"+name+L"的效果状态";
    timeline.Log(text.c_str(),int((c.opcode==2||c.opcode==7||c.opcode==8)?0:code),uint32_t(timeline.Number()+1));
   };if(group.empty())record(command);else for(const auto& c:group)record(c);
  }
  DuelEditor::Completed(result);if(result==EDITOR_OK)return true;RefreshEditorState();}
 if(response){viewPromptWaiting=false;viewPrompt.clear();DuelEditor::SetPromptPlayer(2);last_replay_response_size=last_replay.WriteResponse(resp.data(),length);if(!last_replay_response_size){DuelEditor::Completed(8);return false;}timeline.Response(resp.data(),length);set_responseb(pduel,resp.data());return true;}
 return false;
}
bool SingleMode::AtBoundary(){return atBoundary.load();}
void SingleMode::Gate(){
 if(!DuelEditor::active || booting || editReflow)return;
 atBoundary=true;RefreshEditorState();
 for(;;){if(!is_continuing)return;if(ServiceActions(false))return;if(!paused){atBoundary=false;return;}if(stepBudget){stepBudget=0;atBoundary=false;return;}mainGame->singleSignal.Wait();RefreshEditorState();}
}
void SingleMode::RefreshEditorState() {
 bool safe=editor_can_edit(pduel)!=0;
 if(DuelEditor::active) {
  std::vector<editor_card_info> cards(editor_list_cards(pduel,nullptr,0));
  editor_list_cards(pduel,cards.data(),uint32_t(cards.size()));
  DuelEditor::Cards(cards,editor_revision(pduel));
  std::vector<editor_effect_info> effects(editor_list_custom_effects(pduel,nullptr,0));editor_list_custom_effects(pduel,effects.data(),effects.size());DuelEditor::CustomEffects(effects);
  std::vector<editor_material_info> materials(editor_list_materials(pduel,nullptr,0));editor_list_materials(pduel,materials.data(),uint32_t(materials.size()));std::vector<editor_counter_info> counters(editor_list_counters(pduel,nullptr,0));editor_list_counters(pduel,counters.data(),uint32_t(counters.size()));DuelEditor::Attachments(materials,counters);
 }
 DuelEditor::Available(safe,editor_revision(pduel),timeline.CanBack());
 DuelEditor::Transport(paused,timeline.CanBack(),timeline.CanForward(),atBoundary,timeline.Number());
}
void SingleMode::AnalyzeAndWait(unsigned char* msg,unsigned int len) {
 if(DuelEditor::active){timeline.Prompt(msg,len);if(editor_can_edit(pduel)){booting=false;editReflow=false;}atBoundary=true;}
 DuelEditor::Available(false,editor_revision(pduel));
 if(DuelEditor::active){viewPrompt.assign(msg,msg+len);int actor=len>1?msg[1]:2;if(msg[0]==MSG_SELECT_SUM)actor=len>2?msg[2]:2;DuelEditor::SetPromptPlayer(actor<2?actor:2);}
 bool wait=!DuelClient::ClientAnalyze(msg,len);viewPromptWaiting=wait;if(DuelEditor::active){if(wait)DuelEditor::MaskPendingPrompt();else DuelEditor::SetPromptPlayer(2);}
 if(wait)RefreshEditorState();
 if(DuelEditor::active){
  for(;;){if(!is_continuing)break;if(ServiceActions(true))break;RefreshEditorState();mainGame->singleSignal.Wait();}
  atBoundary=false;DuelEditor::Available(false,editor_revision(pduel));return;
 }
 for(;;) {
  if(wait)mainGame->singleSignal.Wait();
  if(!is_continuing)break;
  editor_command cmd{};std::array<unsigned char,SIZE_RETURN_VALUE> resp{};
  bool edit=false,response=false,undo=false;unsigned int responseLength=0;
  {
   std::lock_guard<std::mutex> lock(inputMutex);
   edit=editPending;response=responsePending;undo=undoPending;cmd=pendingEdit;
   resp=pendingResponse;responseLength=pendingLength;
   editPending=false;responsePending=false;undoPending=false;
  }
  if(undo) {
   bool restored=editorHistory.Undo(pduel,last_replay);
   last_replay_response_size=0;
   DuelEditor::Completed(restored?EDITOR_OK:EDITOR_UNSAFE);
   if(restored)break;
   RefreshEditorState();
   wait=true;continue;
  }
  if(edit) {
   int result=last_replay.CanWriteEditorCommand()?editor_apply(pduel,&cmd):8;
   if(result==EDITOR_OK) {last_replay.WriteEditorCommand(cmd);editorHistory.RecordEdit(cmd,sizeof(editor_command)+2);last_replay_response_size=0;}
   DuelEditor::Completed(result);
   if(result==EDITOR_OK)break;
   RefreshEditorState();
   wait=true;continue;
  }
  if(response) {
   last_replay_response_size=last_replay.WriteResponse(resp.data(),responseLength);
   if(DuelEditor::active && !last_replay_response_size) {
    DuelEditor::Completed(8);
    DuelClient::ClientAnalyze(msg,len);
    RefreshEditorState();
    wait=true;continue;
   }
   if(DuelEditor::active)editorHistory.RecordResponse(resp.data(),responseLength,last_replay_response_size);
   set_responseb(pduel,resp.data());break;
  }
  if(!wait)break;
 }
 DuelEditor::Available(false,editor_revision(pduel));
}
void SingleMode::SinglePlayThread() {
	const int start_lp = 8000;
	const int start_hand = 5;
	const int draw_count = 1;
	mainGame->dInfo.Clear();
	const bool editorSession=DuelEditor::active.load();
	int opt = 0;
	std::random_device rd;
	ExtendedReplayHeader rh;
	rh.base.id = REPLAY_ID_YRP2;
	rh.base.version = PRO_VERSION;
	rh.base.flag = REPLAY_UNIFORM | REPLAY_SINGLE_MODE;
	if(editorSession){rh.base.flag|=REPLAY_EDITOR;rh.value1=6;}
	rh.base.start_time = (uint32_t)std::time(nullptr);
	for (auto& x : rh.seed_sequence)
		x = rd();
	std::seed_seq seed(rh.seed_sequence, rh.seed_sequence + SEED_COUNT);
	std::mt19937 rnd(seed);
	uint32_t duel_seed[SEED_COUNT]{};
	for (auto& x : duel_seed)
		x = rnd();
	set_script_reader(DataManager::ScriptReaderEx);
	set_card_reader(DataManager::CardReader);
	set_message_handler(SingleMode::MessageHandler);
	if(editorSession)std::copy(duel_seed,duel_seed+SEED_COUNT,rh.seed_sequence);
	pduel = create_duel_v2(duel_seed);
	editor_enable(pduel,editorSession?3:0);
	set_player_info(pduel, 0, start_lp, start_hand, draw_count);
	set_player_info(pduel, 1, start_lp, start_hand, draw_count);
	mainGame->dInfo.lp[0] = start_lp;
	mainGame->dInfo.lp[1] = start_lp;
	mainGame->dInfo.start_lp = start_lp;
	myswprintf(mainGame->dInfo.strLP[0], L"%d", mainGame->dInfo.lp[0]);
	myswprintf(mainGame->dInfo.strLP[1], L"%d", mainGame->dInfo.lp[1]);
	BufferIO::CopyWideString(mainGame->ebNickName->getText(), mainGame->dInfo.hostname);
	mainGame->dInfo.clientname[0] = 0;
 if(editorSession)DuelEditor::RefreshPlayerNames();
	mainGame->dInfo.player_type = 0;
	mainGame->dInfo.turn = 0;
	if(mainGame->chkSinglePlayReturnDeckTop->isChecked())
		opt |= DUEL_RETURN_DECK_TOP;
	char filename[256]{};
	int slen = 0;
	if(editorSession) {
  mainGame->open_file=false;std::strcpy(filename,"./single/duel_editor.lua");slen=std::strlen(filename);
  if(!DuelEditor::initialSetup.Initialize(pduel))slen=0;
 } else if(mainGame->open_file) {
		mainGame->open_file = false;
		slen = BufferIO::EncodeUTF8(mainGame->open_file_name, filename);
		if(!preload_script(pduel, filename)) {
			wchar_t fname[256]{};
			myswprintf(fname, L"./single/%ls", mainGame->open_file_name);
			slen = BufferIO::EncodeUTF8(fname, filename);
			if(!preload_script(pduel, filename))
				slen = 0;
		}
	} else {
		const wchar_t* name = mainGame->lstSinglePlayList->getListItem(mainGame->lstSinglePlayList->getSelected());
		wchar_t fname[256]{};
		myswprintf(fname, L"./single/%ls", name);
		slen = BufferIO::EncodeUTF8(fname, filename);
		if(!preload_script(pduel, filename))
			slen = 0;
	}
	if(slen == 0) {
		end_duel(pduel);
		if(editorSession){DuelEditor::Shutdown();std::lock_guard<std::mutex> lock(mainGame->gMutex);mainGame->wMainMenu->setVisible(true);}
		return;
	}
	mainGame->gMutex.lock();
	mainGame->HideElement(mainGame->wSinglePlay);
	if(editorSession)mainGame->HideElement(mainGame->wMainMenu);
	mainGame->ClearCardInfo();
	mainGame->wCardImg->setVisible(true);
	mainGame->wInfos->setVisible(true);
	mainGame->btnLeaveGame->setVisible(true);
	mainGame->btnLeaveGame->setText(dataManager.GetSysString(1210));
	mainGame->wPhase->setVisible(true);
	mainGame->dField.Clear();
	mainGame->dInfo.isFirst = true;viewPrompt.clear();viewPromptWaiting=false;viewPending=false;DuelEditor::SetPromptPlayer(2);
	mainGame->dInfo.isStarted = true;
	mainGame->dInfo.isFinished = false;
	mainGame->dInfo.isSingleMode = true;
	mainGame->device->setEventReceiver(&mainGame->dField);
	mainGame->gMutex.unlock();
	std::vector<unsigned char> engineBuffer;
	engineBuffer.resize(SIZE_MESSAGE_BUFFER);
	is_closing = false;
	is_continuing = true;
	int len = get_message(pduel, engineBuffer.data());
	if (len > 0)
		is_continuing = SinglePlayAnalyze(engineBuffer.data(), len);
	last_replay.BeginRecord(editorSession);
	last_replay_response_size = 0;
	last_replay.WriteHeader(rh);
	uint16_t host_name[20]{};
	BufferIO::CopyCharArray(mainGame->dInfo.hostname, host_name);
	last_replay.WriteData(host_name, sizeof host_name, false);
	uint16_t client_name[20]{};
	BufferIO::CopyCharArray(mainGame->dInfo.clientname, client_name);
	last_replay.WriteData(client_name, sizeof client_name, false);
	last_replay.WriteInt32(start_lp, false);
	last_replay.WriteInt32(start_hand, false);
	last_replay.WriteInt32(draw_count, false);
	last_replay.WriteInt32(opt, false);
	last_replay.Write<uint16_t>(slen, false);
	last_replay.WriteData(filename, slen, false);
 if(editorSession) {
  last_replay.WriteInt32(DuelEditor::initialSetup.hand,false);
  last_replay.WriteInt32(DuelEditor::initialSetup.hand1,false);
  for(const auto& deck:DuelEditor::initialSetup.decks)for(const auto* list:{&deck.main,&deck.extra}){last_replay.WriteInt32(list->size(),false);last_replay.WriteData(list->data(),list->size()*sizeof(uint32_t),false);}
 }
	last_replay.Flush();
	if(editorSession){editorHistory.Reset(duel_seed,filename,opt);editorHistory.SetInitialSetup(DuelEditor::initialSetup);timeline.Reset(duel_seed,DuelEditor::initialSetup,opt,last_replay);}
	start_duel(pduel, opt);
 if(editorSession){timeline.InitialPoint(pduel);booting=false;}
 bool restoredWaiting=false;
	while (is_continuing) {
  try {
   if(restoredWaiting){restoredWaiting=false;auto prompt=timeline.CurrentPrompt();if(!prompt.empty()){AnalyzeAndWait(prompt.data(),prompt.size());continue;}}
   Gate();if(!is_continuing)break;
   unsigned int result=process(pduel);len=result&PROCESSOR_BUFFER_LEN;
   if(len>int(engineBuffer.size()))engineBuffer.resize(len);
   if(len)get_message(pduel,engineBuffer.data());
   if(editorSession){if(editor_can_edit(pduel))editReflow=false;bool point=(!booting && !editReflow) || editor_can_edit(pduel);timeline.Processed(pduel,result,point);if(point)timeline.RestoreLogs();}
   if(len)is_continuing=SinglePlayAnalyze(engineBuffer.data(),len);
  } catch(const EditorJump&){restoredWaiting=timeline.Waiting();booting=false;editReflow=false;atBoundary=true;}
	}
	if(editorSession){last_replay.pheader.value2=timeline.ProcessCount();last_replay.pheader.value3=1;auto data=last_replay.CaptureRecording();if(data.size()>=80){uint16_t names[2][20]{};for(int i=0;i<2;++i)BufferIO::CopyCharArray(DuelEditor::PlayerName(i).c_str(),names[i]);std::memcpy(data.data(),names,sizeof names);last_replay.ReplaceRecording(last_replay.pheader,data);}}
	last_replay.EndRecord();
 if(editorSession)DuelEditor::Shutdown();
	mainGame->gMutex.lock();
	time_t nowtime = std::time(nullptr);
	wchar_t timetext[40];
	std::wcsftime(timetext, sizeof timetext / sizeof timetext[0], L"%Y-%m-%d %H-%M-%S", std::localtime(&nowtime));
	mainGame->ebRSName->setText(timetext);
	if(!is_closing && (editorSession || !mainGame->chkAutoSaveReplay->isChecked())) {
  mainGame->actionParam=0;mainGame->replaySignal.Reset();
  mainGame->btnRSNo->setText(editorSession?L"不保存":dataManager.GetSysString(1212));
		mainGame->wReplaySave->setText(dataManager.GetSysString(1340));
		mainGame->PopupElement(mainGame->wReplaySave);
		mainGame->gMutex.unlock();
		mainGame->replaySignal.Wait();
	} else if(is_closing) {mainGame->actionParam=0;mainGame->gMutex.unlock();
	} else {
		mainGame->actionParam = 1;
		wchar_t msgbuf[256];
		myswprintf(msgbuf, dataManager.GetSysString(1367), timetext);
		mainGame->SetStaticText(mainGame->stACMessage, 310, mainGame->guiFont, msgbuf);
		mainGame->PopupElement(mainGame->wACMessage, 20);
		mainGame->gMutex.unlock();
		mainGame->WaitFrameSignal(30);
	}
	if(mainGame->actionParam)
		last_replay.SaveReplay(mainGame->ebRSName->getText());
	end_duel(pduel);
	pduel=0;
	if(!is_closing) {
		mainGame->gMutex.lock();
		mainGame->dInfo.isStarted = false;
		mainGame->dInfo.isInDuel = false;
		mainGame->dInfo.isFinished = true;
		mainGame->dInfo.isSingleMode = false;
		mainGame->gMutex.unlock();
		mainGame->closeDoneSignal.Reset();
		mainGame->closeSignal.Set();
		mainGame->closeDoneSignal.Wait();
		mainGame->gMutex.lock();
		mainGame->ShowElement(editorSession?mainGame->wMainMenu:mainGame->wSinglePlay);
		mainGame->stTip->setVisible(false);
		mainGame->device->setEventReceiver(&mainGame->menuHandler);
		mainGame->gMutex.unlock();
		if(mainGame->exit_on_return)
			mainGame->device->closeDevice();
	}
}
bool SingleMode::SinglePlayAnalyze(unsigned char* msg, unsigned int len) {
	unsigned char* offset, * pbuf = msg;
	int player, count;
	while (pbuf - msg < (int)len) {
		if(is_closing || !is_continuing)
			return false;
		offset = pbuf;
		mainGame->dInfo.curMsg = BufferIO::Read<uint8_t>(pbuf);
		switch (mainGame->dInfo.curMsg) {
		case MSG_RETRY: {
			if(last_replay_response_size) {
				if(last_replay.RemoveData(last_replay_response_size) && DuelEditor::active)editorHistory.DiscardRejectedResponse();
				last_replay_response_size = 0;
			}
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_HINT: {
			int type = BufferIO::Read<uint8_t>(pbuf);
			player = BufferIO::Read<uint8_t>(pbuf);
			/*int data = */BufferIO::Read<int32_t>(pbuf);
			switch (type) {
			case HINT_EVENT:
			case HINT_MESSAGE:
			case HINT_SELECTMSG:
			case HINT_EFFECT: {
				if(player == 0)
					DuelClient::ClientAnalyze(offset, pbuf - offset);
				break;
			}
			case HINT_OPSELECTED:
			case HINT_NUMBER:
			case HINT_RACE:
			case HINT_ATTRIB:
			case HINT_CODE: {
				if(player == 1)
					DuelClient::ClientAnalyze(offset, pbuf - offset);
				break;
			}
			case HINT_CARD:
			case HINT_ZONE: {
				DuelClient::ClientAnalyze(offset, pbuf - offset);
				break;
			}
			}
			break;
		}
		case MSG_WIN: {
			pbuf += 2;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			return false;
		}
		case MSG_SELECT_BATTLECMD: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 11;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 8 + 2;
			SinglePlayRefresh();
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_IDLECMD: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 11 + 3;
			SinglePlayRefresh();
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_EFFECTYN: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 12;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_YESNO: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 4;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_OPTION: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 4;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_CARD:
		case MSG_SELECT_TRIBUTE: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 3;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 8;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_UNSELECT_CARD: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 4;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 8;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 8;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_CHAIN: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 9 + count * 14;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_PLACE:
		case MSG_SELECT_DISFIELD: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 5;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_POSITION: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 5;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_COUNTER: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 4;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 9;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SELECT_SUM: {
			pbuf++;
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 6;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 11;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 11;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_SORT_CARD: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_CONFIRM_DECKTOP: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CONFIRM_EXTRATOP: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CONFIRM_CARDS: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 1;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SHUFFLE_DECK: {
			player = BufferIO::Read<uint8_t>(pbuf);
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshDeck(player);
			break;
		}
		case MSG_SHUFFLE_HAND: {
			/*int oplayer = */BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SHUFFLE_EXTRA: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_REFRESH_DECK: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SWAP_GRAVE_DECK: {
			player = BufferIO::Read<uint8_t>(pbuf);
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshGrave(player);
			break;
		}
		case MSG_REVERSE_DECK: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshDeck(0);
			SinglePlayRefreshDeck(1);
			break;
		}
		case MSG_DECK_TOP: {
			pbuf += 6;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SHUFFLE_SET_CARD: {
			pbuf++;
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_NEW_TURN: {
			player = BufferIO::Read<uint8_t>(pbuf);
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_NEW_PHASE: {
			pbuf += 2;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_MOVE: {
			int pc = pbuf[4];
			int pl = pbuf[5];
			/*int ps = pbuf[6];*/
			/*int pp = pbuf[7];*/
			int cc = pbuf[8];
			int cl = pbuf[9];
			int cs = pbuf[10];
			/*int cp = pbuf[11];*/
			pbuf += 16;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			if(cl && !(cl & LOCATION_OVERLAY) && (pl != cl || pc != cc))
				SinglePlayRefreshSingle(cc, cl, cs);
			break;
		}
		case MSG_POS_CHANGE: {
			pbuf += 9;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SET: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SWAP: {
			pbuf += 16;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_FIELD_DISABLED: {
			pbuf += 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SUMMONING: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SUMMONED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_SPSUMMONING: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SPSUMMONED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_FLIPSUMMONING: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_FLIPSUMMONED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_CHAINING: {
			pbuf += 16;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CHAINED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_CHAIN_SOLVING: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CHAIN_SOLVED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_CHAIN_END: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			SinglePlayRefreshDeck(0);
			SinglePlayRefreshDeck(1);
			break;
		}
		case MSG_CHAIN_NEGATED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CHAIN_DISABLED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CARD_SELECTED:
		case MSG_RANDOM_SELECTED: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_BECOME_TARGET: {
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_DRAW: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
   if(DuelEditor::active&&reinterpret_cast<duel*>(pduel)->game_field->infos.turn_id==0){std::wstring title=L"玩家 "+std::to_wstring(player+1)+L" 开局手牌："+std::to_wstring(count)+L" 张";mainGame->AddLog(title.c_str());auto* cards=pbuf;for(int i=0;i<count;++i){uint32_t code=BufferIO::Read<uint32_t>(cards)&0x7fffffffU;mainGame->AddLog(dataManager.GetName(code),int(code));}}
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_DAMAGE: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_RECOVER: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_EQUIP: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_LPUPDATE: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_UNEQUIP: {
			pbuf += 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CARD_TARGET: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CANCEL_TARGET: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_PAY_LPCOST: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ADD_COUNTER: {
			pbuf += 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_REMOVE_COUNTER: {
			pbuf += 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ATTACK: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_BATTLE: {
			pbuf += 26;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ATTACK_DISABLED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_DAMAGE_STEP_START: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_DAMAGE_STEP_END: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_MISSED_EFFECT: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_TOSS_COIN: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_TOSS_DICE: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ROCK_PAPER_SCISSORS: {
			player = BufferIO::Read<uint8_t>(pbuf);
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_HAND_RES: {
			pbuf += 1;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ANNOUNCE_RACE: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 5;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_ANNOUNCE_ATTRIB: {
			player = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 5;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_ANNOUNCE_CARD:
		case MSG_ANNOUNCE_NUMBER: {
			player = BufferIO::Read<uint8_t>(pbuf);
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += 4 * count;
			AnalyzeAndWait(offset, pbuf - offset);
			break;
		}
		case MSG_CARD_HINT: {
			pbuf += 9;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_PLAYER_HINT: {
			pbuf += 6;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_TAG_SWAP: {
			player = pbuf[0];
			pbuf += pbuf[2] * 4 + pbuf[4] * 4 + 9;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshDeck(player);
			SinglePlayRefreshExtra(player);
			break;
		}
		case MSG_MATCH_KILL: {
			pbuf += 4;
			break;
		}
		case MSG_RELOAD_FIELD: {
			pbuf++;
			for(int p = 0; p < 2; ++p) {
				pbuf += 4;
				for(int seq = 0; seq < 7; ++seq) {
					int val = BufferIO::Read<uint8_t>(pbuf);
					if(val)
						pbuf += 2;
				}
				for(int seq = 0; seq < 8; ++seq) {
					int val = BufferIO::Read<uint8_t>(pbuf);
					if(val)
						pbuf++;
				}
				pbuf += 6;
			}
			count = BufferIO::Read<uint8_t>(pbuf);
			pbuf += count * 15;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayReload();
			mainGame->gMutex.lock();
			mainGame->dField.RefreshAllCards();
			mainGame->gMutex.unlock();
			break;
		}
		case MSG_AI_NAME: {
			char namebuf[SIZE_AI_NAME]{};
			wchar_t wname[20]{};
			int name_len = BufferIO::Read<uint16_t>(pbuf);
			if (name_len + 1 <= (int)sizeof namebuf) {
				std::memcpy(namebuf, pbuf, name_len);
				namebuf[name_len] = 0;
			}
			pbuf += name_len + 1;
			BufferIO::DecodeUTF8(namebuf, wname);
			if(!DuelEditor::active)BufferIO::CopyCharArray(wname, mainGame->dInfo.clientname);
			break;
		}
		case MSG_SHOW_HINT: {
			char msgbuf[SIZE_HINT_MSG]{};
			wchar_t msg[SIZE_HINT_MSG]{};
			int msg_len = BufferIO::Read<uint16_t>(pbuf);
			if (msg_len + 1 <= (int)sizeof msgbuf) {
				std::memcpy(msgbuf, pbuf, msg_len);
				msgbuf[msg_len] = 0;
			}
			pbuf += msg_len + 1;
			BufferIO::DecodeUTF8(msgbuf, msg);
			mainGame->gMutex.lock();
			mainGame->SetStaticText(mainGame->stMessage, 310, mainGame->guiFont, msg);
			mainGame->PopupElement(mainGame->wMessage);
			mainGame->gMutex.unlock();
			mainGame->actionSignal.Reset();
			mainGame->actionSignal.Wait();
			break;
		}
		}
	}
	return is_continuing;
}
inline void SingleMode::ReloadLocation(int player, int location, int flag, std::vector<unsigned char>& queryBuffer) {
	query_field_card(pduel, player, location, flag, queryBuffer.data(), 0);
	std::lock_guard<std::mutex> lock(mainGame->gMutex);
	mainGame->dField.UpdateFieldCard(mainGame->LocalPlayer(player), location, queryBuffer.data());
}
void SingleMode::SinglePlayRefresh(int flag) {
 if(DuelEditor::active)flag|=QUERY_COUNTERS|QUERY_OVERLAY_CARD;
	std::vector<unsigned char> queryBuffer;
	queryBuffer.resize(1024*1024);
	ReloadLocation(0, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_HAND, flag, queryBuffer);
	ReloadLocation(1, LOCATION_HAND, flag, queryBuffer);
}
void SingleMode::SingleRefreshLocation(int player, int location, int flag) {
	std::vector<unsigned char> queryBuffer;
	queryBuffer.resize(1024*1024);
	ReloadLocation(player, location, flag, queryBuffer);
}
inline void SingleMode::SinglePlayRefreshHand(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_HAND, flag);
}
inline void SingleMode::SinglePlayRefreshGrave(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_GRAVE, flag);
}
inline void SingleMode::SinglePlayRefreshDeck(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_DECK, flag);
}
inline void SingleMode::SinglePlayRefreshExtra(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_EXTRA, flag);
}
void SingleMode::SinglePlayRefreshSingle(int player, int location, int sequence, int flag) {
	unsigned char queryBuffer[0x1000];
	/*int len = */query_card(pduel, player, location, sequence, flag, queryBuffer, 0);
	std::lock_guard<std::mutex> lock(mainGame->gMutex);
	mainGame->dField.UpdateCard(mainGame->LocalPlayer(player), location, sequence, queryBuffer);
}
void SingleMode::SinglePlayReload() {
	std::vector<unsigned char> queryBuffer;
	queryBuffer.resize(1024*1024);
	unsigned int flag = 0xffdfff;
	ReloadLocation(0, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_HAND, flag, queryBuffer);
	ReloadLocation(1, LOCATION_HAND, flag, queryBuffer);

	ReloadLocation(0, LOCATION_DECK, flag, queryBuffer);
	ReloadLocation(1, LOCATION_DECK, flag, queryBuffer);
	ReloadLocation(0, LOCATION_EXTRA, flag, queryBuffer);
	ReloadLocation(1, LOCATION_EXTRA, flag, queryBuffer);
	ReloadLocation(0, LOCATION_GRAVE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_GRAVE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_REMOVED, flag, queryBuffer);
	ReloadLocation(1, LOCATION_REMOVED, flag, queryBuffer);
}
uint32_t SingleMode::MessageHandler(intptr_t fduel, uint32_t type) {
	char msgbuf[1024];
	get_log_message(fduel, msgbuf);
	mainGame->AddDebugMsg(msgbuf);
	return 0;
}

}
