#include "duel_editor.h"
#include "game.h"
#include "single_mode.h"
#include "duelclient.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "file_system.h"
#include "editor_timeline.h"
#include "editor_paths.h"
#include "editor_effect_labels.h"
#include "../ocgcore/editor_rules.h"
#include <map>
#include <ctime>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cwchar>
#include <limits>
#include <windows.h>

namespace ygo {
std::atomic<bool> DuelEditor::active{false};
namespace {std::wstring playerNames[2]={L"玩家 1",L"玩家 2"};irr::gui::IGUIEditBox* nameInputs[2]{};}
EditorSetup DuelEditor::initialSetup;
namespace {
constexpr int TOGGLE=9000, SEARCH=9001, RESULTS=9002, CREATE=9003, CONTINUE=9004, LP=9005, REGION=9006, UNDO=9007, MODE=9015, EXISTING=9016, DELETE_CARD=9017;
irr::gui::IGUIButton* toggle{};irr::gui::IGUIButton* viewButton{};std::atomic<uint32_t> viewPlayer{0},timingModes[2]{};std::atomic<int> promptPlayer{2};bool timingsInitialized=false;
irr::gui::IGUIWindow* panel{};
irr::gui::IGUIEditBox *search{}, *slot{}, *lp{};
irr::gui::IGUIListBox *results{}, *existing{};
irr::gui::IGUIComboBox *player{}, *location{}, *position{}, *summonTurn{}, *mode{}, *summonSource{};
irr::gui::IGUICheckBox *ownerOther{}, *procedure{}, *specialOrigin{}, *normalOrigin{}, *flipOrigin{};
irr::gui::IGUIStaticText *status{}, *searchLabel{}, *slotLabel{}, *slotTooltip{};std::wstring slotExplanation;
irr::gui::IGUIButton* searchButton{};
irr::gui::IGUIButton *create{}, *setLP{}, *undo{}, *deleteButton{};
std::vector<uint32_t> codes;
bool available=false;
uint32_t revision=0;
uint64_t selectedID=0;
uint32_t formPlayer=0;
std::vector<editor_card_info> currentCards;
std::recursive_mutex editorGuiMutex;
irr::gui::IGUIWindow *setupWindow{},*orderWindow{};
irr::gui::IGUIComboBox *setupCategories[2]{},*setupDecks[2]{},*orderPlayer{},*orderZone{};
irr::gui::IGUIStaticText *setupStatus{},*orderStatus{};
irr::gui::IGUIListBox* orderList{};
irr::gui::IGUIEditBox* orderTarget{};
irr::gui::IGUIButton* orderToggle{};
std::vector<editor_card_info> orderedCards;
irr::gui::IGUIEditBox* startHand[2]{};
irr::gui::IGUIButton *pauseButton{},*stepButton{},*saveButton{};
irr::gui::IGUIStaticText *transportText{},*saveStatus{};
irr::gui::IGUIWindow *saveWindow{},*formWindow{};
irr::gui::IGUIListBox *saveList{};
irr::gui::IGUIEditBox* saveLabel{};
irr::gui::IGUIComboBox *cardForm{},*raceBox{},*attributeBox{},*nameBox{};irr::gui::IGUIStaticText* nameLabel{};irr::gui::IGUIEditBox* nameSearch{};std::vector<uint32_t> nameCodes;irr::gui::IGUITab* formContent{};irr::gui::IGUIScrollBar* formScroll{};irr::gui::IGUIComboBox* kindBox{};irr::gui::IGUIStaticText* kindLabel{};irr::gui::IGUIButton* otherTypesButton{};irr::gui::IGUIWindow* otherTypesWindow{};irr::gui::IGUIStaticText* arrowsLabel{};irr::gui::IGUICheckBox* arrowBoxes[8]{};const uint32_t arrowMasks[8]={0x40,0x80,0x100,0x8,0x20,0x1,0x2,0x4};
irr::gui::IGUIEditBox *attackBox{},*defenseBox{},*levelBox{};
irr::gui::IGUICheckBox *effectMonster{},*keepSpellTrap{},*selfSummon{},*stateBoxes[4]{},*ruleBoxes[32]{};
std::vector<std::string> saveFiles;
irr::gui::IGUICheckBox* typeBoxes[13]{};irr::gui::IGUIStaticText *historyLabel{},*levelLabel{},*slotHelp{},*summonSourceLabel{};uint32_t sourceChoiceCode=~0U;
const uint32_t typeMasks[]={TYPE_TUNER,TYPE_DUAL,TYPE_FUSION,TYPE_SYNCHRO,TYPE_XYZ,TYPE_LINK,TYPE_RITUAL,TYPE_SPIRIT,TYPE_UNION,TYPE_FLIP,TYPE_TOON,TYPE_PENDULUM,TYPE_SPSUMMON};
std::map<uint32_t,editor_card_info> trapProfiles;std::map<uint32_t,bool> trapChecked;
int Shape(){return cardForm->getItemData(cardForm->getSelected());}
void ComboValue(irr::gui::IGUIComboBox* box,uint32_t value){for(unsigned i=0;i<box->getItemCount();++i)if(box->getItemData(i)==value){box->setSelected(i);return;}box->setSelected(0);}
bool firstEditor=true,spaceDown=false;
irr::gui::IGUIComboBox* extraState{};irr::gui::IGUIEditBox* extraValue{};irr::gui::IGUIListBox* extraList{};std::vector<editor_effect_info> customEffects;
void RefreshCustom(){if(!extraList)return;extraList->clear();for(const auto& e:customEffects)if(e.card_id==selectedID){size_t i=0;for(;i<sizeof(editor_catalog_codes)/sizeof(editor_catalog_codes[0]);++i)if(editor_catalog_codes[i]==e.code)break;std::wstring row=i<sizeof(editor_catalog_codes)/sizeof(editor_catalog_codes[0])?editor_catalog_labels[i]:L"状态效果";row+=L" · "+std::to_wstring(e.value);extraList->addItem(row.c_str());}}
void SubmitExtra(bool remove){if(!available || !selectedID){saveStatus->setText(L"更多状态效果需要先选中已有卡片");return;}editor_command cmd{};cmd.opcode=6;cmd.revision=revision;cmd.code=extraState->getItemData(extraState->getSelected());if(remove && extraList->getSelected()>=0){int row=0;for(const auto& effect:customEffects)if(effect.card_id==selectedID){if(row++==extraList->getSelected()){cmd.code=effect.code;break;}}}cmd.target_low=uint32_t(selectedID);cmd.target_high=uint32_t(selectedID>>32);cmd.position=remove?0:1;const wchar_t* text=extraValue->getText();wchar_t* end=nullptr;auto n=std::wcstoll(text,&end,10);if(!*text || *end || n<INT32_MIN || n>INT32_MAX){saveStatus->setText(L"效果数值必须为有符号整数");return;}cmd.value=uint32_t(n);SingleMode::QueueEditorCommand(cmd);}
const wchar_t* ruleLabels[]={L"卡片被无效",L"效果无效",L"不能发动效果",L"不能攻击",L"不能宣言攻击",L"不能直接攻击",L"不能改变表示形式",L"不能变成里侧",L"不被战斗破坏",L"不被效果破坏",L"不能成为攻击对象",L"不能成为效果对象",L"不能作召唤解放",L"不能作其他解放",L"不能回手牌",L"不能回卡组",L"不能送去墓地",L"不能除外",L"不能作为代价",L"不能作送墓代价",L"不能转移控制权",L"不能被无效",L"发动不能被无效",L"效果不能被无效",L"陷阱可从手牌发动",L"陷阱盖放回合可发动",L"可以直接攻击",L"已是二重效果怪兽",L"不能作融合素材",L"不能作同调素材",L"不能作超量素材",L"不能作连接素材"};
const uint32_t stateMasks[]={0x10U,0x20U,0x100U,0x4000000U};
const wchar_t* stateLabels[]={L"本回合盖放",L"没有等级",L"本回合不能手动变更表示",L"被禁止令适用"};
irr::gui::IGUIComboBox* saveFolder{};irr::gui::IGUIEditBox* folderName{};std::vector<std::wstring> saveFolders;std::wstring saveRoot;irr::gui::IGUIWindow* saveDeleteWindow{};irr::gui::IGUIStaticText* saveDeleteQuestion{};std::wstring deleteSavePath;bool deleteSaveFolder=false;irr::gui::IGUIWindow* drawWindow{};irr::gui::IGUIEditBox* drawCount{};irr::gui::IGUIStaticText* drawStatus{};uint32_t drawPlayer=0;bool startupSaveSelection=false;
std::string Utf8Path(const std::wstring& path){char bytes[4096]{};BufferIO::EncodeUTF8(path.c_str(),bytes);return bytes;}
void EnsureSaveRoot(){if(saveRoot.empty())saveRoot=EditorPaths::SaveRoot(EditorPaths::RuntimeRoot());EditorPaths::CreateDirectories(saveRoot); }
std::wstring CurrentSaveFolder(){EnsureSaveRoot();int i=saveFolder->getSelected();return saveRoot+(i>0 && size_t(i)<saveFolders.size()?L"/"+saveFolders[i]:L"");}
void RefreshSaveFolders(){EnsureSaveRoot();std::wstring previous;int selected=saveFolder->getSelected();if(selected>0 && size_t(selected)<saveFolders.size())previous=saveFolders[selected];saveFolders={L""};FileSystem::TraversalDir(saveRoot.c_str(),[](const wchar_t* name,bool dir){if(dir && std::wstring(name)!=L"." && std::wstring(name)!=L"..")saveFolders.emplace_back(name);});std::sort(saveFolders.begin()+1,saveFolders.end());saveFolder->clear();saveFolder->addItem(L"根文件夹",0);for(size_t i=1;i<saveFolders.size();++i)saveFolder->addItem(saveFolders[i].c_str(),i);auto found=std::find(saveFolders.begin(),saveFolders.end(),previous);saveFolder->setSelected(found==saveFolders.end()?0:int(found-saveFolders.begin()));}
bool SafeSaveDeletePath(const std::wstring& path,std::wstring& result){wchar_t root[4096]{},target[4096]{};auto rootLength=GetFullPathNameW(saveRoot.c_str(),4096,root,nullptr),targetLength=GetFullPathNameW(path.c_str(),4096,target,nullptr);if(!rootLength || rootLength>=4096 || !targetLength || targetLength>=4096)return false;std::wstring prefix=root;while(!prefix.empty() && (prefix.back()==L'/' || prefix.back()==L'\\'))prefix.pop_back();prefix+=L"\\";result=target;if(result.size()<=prefix.size() || _wcsnicmp(result.c_str(),prefix.c_str(),prefix.size()))return false;DWORD attrs=GetFileAttributesW(result.c_str());return attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_REPARSE_POINT);}
bool CountSaveFolder(const std::wstring& folder,size_t& points,size_t& files){bool valid=true;FileSystem::TraversalDir(folder.c_str(),[&](const wchar_t* name,bool dir){std::wstring path; if(!SafeSaveDeletePath(folder+L"/"+name,path)){valid=false;return;}if(dir){if(!CountSaveFolder(path,points,files))valid=false;}else{++files;std::wstring n=name;if(n.size()>=5 && n.substr(n.size()-5)==L".yges")++points;}});return valid;}
void RefreshSaves(){
 RefreshSaveFolders();static_cast<irr::gui::IGUIButton*>(saveWindow->getElementFromId(9514,true))->setEnabled(saveFolder->getSelected()>0);auto folder=CurrentSaveFolder();saveFiles.clear();saveList->clear();FileSystem::TraversalDir(folder.c_str(),[&](const wchar_t* name,bool dir){if(dir)return;std::wstring n=name;if(n.size()<5 || n.substr(n.size()-5)!=L".yges")return;saveFiles.push_back(Utf8Path(folder+L"/"+n));});std::sort(saveFiles.rbegin(),saveFiles.rend());
 for(const auto& file:saveFiles){std::string label=EditorTimeline::ReadName(file);wchar_t text[512]{};BufferIO::DecodeUTF8(label.empty()?"无法读取的存档":label.c_str(),text);saveList->addItem(text);}
}

void RefreshOrder() {
 uint64_t id=0;int selected=orderList->getSelected();if(selected>=0 && selected<int(orderedCards.size()))id=orderedCards[selected].id;
 orderedCards.clear();uint32_t p=orderPlayer->getSelected(),zone=orderZone->getItemData(orderZone->getSelected());
 for(const auto& card:currentCards)if(card.player==p && card.location==zone)orderedCards.push_back(card);
 std::sort(orderedCards.begin(),orderedCards.end(),[zone](const editor_card_info& a,const editor_card_info& b){return zone==LOCATION_DECK?a.sequence>b.sequence:a.sequence<b.sequence;});
 orderList->clear();int match=-1;
 for(size_t i=0;i<orderedCards.size();++i){std::wstring text=std::to_wstring(i+1)+L" · "+dataManager.GetName(orderedCards[i].code)+L" ["+std::to_wstring(orderedCards[i].code)+L"] #"+std::to_wstring(orderedCards[i].id);if(i==0 && zone==LOCATION_DECK)text=L"牌顶 → "+text;if(i+1==orderedCards.size() && zone==LOCATION_DECK)text+=L" ← 牌底";orderList->addItem(text.c_str());if(orderedCards[i].id==id)match=i;}
 orderList->setSelected(match);
}
void SortDeck(int action) {
 if(!available){orderStatus->setText(L"当前可查看顺序；排序请等待阶段安全停点");return;}
 int index=orderList->getSelected();if(index<0 || index>=int(orderedCards.size())){orderStatus->setText(L"请选择一张卡片");return;}
 uint32_t target=0;
 if(action==9306){const wchar_t* text=orderTarget->getText();for(auto* c=text;*c;++c)if(*c<L'0'||*c>L'9'){orderStatus->setText(L"请输入有效的目标序号");return;}wchar_t* end=nullptr;auto n=std::wcstoull(text,&end,10);if(!*text || *end || !n || n>orderedCards.size()){orderStatus->setText(L"目标序号超出牌组范围");return;}target=uint32_t(n-1);}
 else if(action==9307)target=index?index-1:0;
 else if(action==9308)target=std::min(size_t(index+1),orderedCards.size()-1);
 else if(action==9309)target=0;else target=orderedCards.size()-1;
 editor_command cmd{};cmd.opcode=5;cmd.revision=revision;cmd.player=orderPlayer->getSelected();cmd.sequence=target;
 cmd.target_low=uint32_t(orderedCards[index].id);cmd.target_high=uint32_t(orderedCards[index].id>>32);
 if(SingleMode::QueueEditorCommand(cmd)){available=false;orderStatus->setText(L"正在调整顺序…");}
}
void ShuffleOrderedDeck(){
 if(!available){orderStatus->setText(L"请等待阶段安全停点再洗牌");return;}
 editor_command cmd{};cmd.opcode=7;cmd.revision=revision;cmd.player=orderPlayer->getSelected();cmd.location=orderZone->getItemData(orderZone->getSelected());
 if(SingleMode::QueueEditorCommand(cmd)){available=false;orderWindow->getElementFromId(9314,true)->setEnabled(false);orderStatus->setText(L"正在洗牌…");}
}
void DeleteOrderedCard(){
 if(!available){orderStatus->setText(L"请等待阶段安全停点再删除");return;}
 int i=orderList->getSelected();if(i<0 || i>=int(orderedCards.size())){orderStatus->setText(L"请先选择要删除的卡片");return;}
 editor_command cmd{};cmd.opcode=3;cmd.revision=revision;cmd.target_low=uint32_t(orderedCards[i].id);cmd.target_high=uint32_t(orderedCards[i].id>>32);
 if(SingleMode::QueueEditorCommand(cmd)){available=false;for(int id=9306;id<=9310;++id)orderWindow->getElementFromId(id,true)->setEnabled(false);orderWindow->getElementFromId(9313,true)->setEnabled(false);orderStatus->setText(L"正在删除选中的卡片…");}
}
void ApplySetupNames(){for(int i=0;i<2;++i)playerNames[i]=nameInputs[i]->getText()[0]?nameInputs[i]->getText():(i?L"玩家 2":L"玩家 1");}
void StartFromSave(const std::string& file){if(EditorTimeline::ReadName(file).empty()){saveStatus->setText(L"无法读取该存档，请选择有效存档");return;}ApplySetupNames();DuelEditor::initialSetup=EditorSetup{};DuelEditor::initialSetup.hand1=0;setupWindow->setVisible(false);saveWindow->setVisible(false);startupSaveSelection=false;DuelEditor::active=true;mainGame->open_file=true;BufferIO::CopyWideString(L"./single/duel_editor.lua",mainGame->open_file_name);SingleMode::StartPlay(file);}
void StartConfiguredDuel() {
 EditorSetup setup;std::wstring error;
 for(int p=0;p<2;++p){if(setupDecks[p]->getSelected()<0){setupStatus->setText(L"请分别选择双方卡组；可将 .ydk 放到 deck 文件夹");return;}
  wchar_t file[256]{};DeckManager::GetDeckFile(file,setupCategories[p]->getSelected(),setupCategories[p]->getText(),setupDecks[p]->getText());
  if(!LoadEditorDeck(file,setup.decks[p],error)){setupStatus->setText((L"玩家 "+std::to_wstring(p+1)+L"："+error).c_str());return;}
 }
 uint32_t hands[2]{};for(int p=0;p<2;++p){const wchar_t* text=startHand[p]->getText();wchar_t* end=nullptr;auto value=std::wcstoull(text,&end,10);bool digits=*text!=0;for(auto* c=text;*c;++c)if(*c<L'0'||*c>L'9')digits=false;if(!digits || *end || value>254){setupStatus->setText(L"起始手牌数量必须为 0～254 的整数");return;}hands[p]=uint32_t(value);}
 ApplySetupNames();setup.hand=hands[0];setup.hand1=hands[1];DuelEditor::initialSetup=std::move(setup);
 setupWindow->setVisible(false);DuelEditor::active=true;mainGame->open_file=true;
 BufferIO::CopyWideString(L"./single/duel_editor.lua",mainGame->open_file_name);SingleMode::StartPlay();
}
const editor_card_info* SelectedCard() {
 for(const auto& card:currentCards)if(card.id==selectedID)return &card;
 return nullptr;
}
void FillExisting();
void SetMode();
bool CurrentData(CardData& data){int i=results->getSelected();return i>=0 && i<int(codes.size()) && dataManager.GetData(codes[i],&data);}
bool TrapProfile(uint32_t code){if(!trapChecked.count(code)){editor_card_info profile{};trapChecked[code]=editor_native_trap_profile(code,&profile)!=0;if(trapChecked[code])trapProfiles[code]=profile;}return trapChecked[code];}
uint32_t CustomType(){uint32_t t=TYPE_MONSTER|kindBox->getItemData(kindBox->getSelected());for(int i=0;i<13;++i)if(typeBoxes[i] && typeBoxes[i]->isChecked())t|=typeMasks[i];if(effectMonster->isChecked())t|=TYPE_EFFECT;else if(!(t&(TYPE_RITUAL|TYPES_EXTRA_DECK)))t|=TYPE_NORMAL;return t;}
uint32_t EffectiveType(){CardData data{};if(!CurrentData(data))return 0;int f=Shape();if(f==1 || f==2)return (f==1?TYPE_SPELL:TYPE_TRAP)|TYPE_CONTINUOUS;if(f==3){uint32_t type=CustomType();if(keepSpellTrap->isChecked()){type|=data.type&(TYPE_SPELL|TYPE_TRAP|TYPE_CONTINUOUS|TYPE_FIELD|TYPE_EQUIP|TYPE_QUICKPLAY|TYPE_COUNTER|TYPE_RITUAL);if(data.type&TYPE_TRAP)type|=TYPE_TRAPMONSTER;}return type;}if(f==4 && TrapProfile(data.code))return trapProfiles[data.code].effective_type;if(f==5)return CustomType();auto* current=SelectedCard();return current && !current->form && (data.type&TYPE_TRAP)?current->effective_type:data.type;}
bool MonsterEffect(uint32_t c){switch(c){case EFFECT_MONSTER_SSET:case EFFECT_CANNOT_SUMMON:case EFFECT_CANNOT_FLIP_SUMMON:case EFFECT_CANNOT_SPECIAL_SUMMON:case EFFECT_CANNOT_MSET:case EFFECT_EXTRA_SUMMON_COUNT:case EFFECT_REVIVE_LIMIT:case EFFECT_EXTRA_SET_COUNT:case EFFECT_UNRELEASABLE_EFFECT:case EFFECT_IGNORE_BATTLE_TARGET:case EFFECT_DUAL_SUMMONABLE:case EFFECT_UPDATE_ATTACK:case EFFECT_SET_ATTACK:case EFFECT_SET_ATTACK_FINAL:case EFFECT_SET_BASE_ATTACK:case EFFECT_UPDATE_DEFENSE:case EFFECT_SET_DEFENSE:case EFFECT_SET_DEFENSE_FINAL:case EFFECT_SET_BASE_DEFENSE:case EFFECT_REVERSE_UPDATE:case EFFECT_SWAP_AD:case EFFECT_SWAP_BASE_AD:case EFFECT_SET_BASE_ATTACK_FINAL:case EFFECT_SET_BASE_DEFENSE_FINAL:case EFFECT_UPDATE_LEVEL:case EFFECT_CHANGE_LEVEL:case EFFECT_UPDATE_RANK:case EFFECT_CHANGE_RANK:case EFFECT_DECREASE_TRIBUTE:case EFFECT_DECREASE_TRIBUTE_SET:case EFFECT_TRIBUTE_LIMIT:case EFFECT_DEFENSE_ATTACK:case EFFECT_ATTACK_DISABLED:case EFFECT_XYZ_LEVEL:case EFFECT_NONTUNER:case EFFECT_CANNOT_SELECT_BATTLE_TARGET:case EFFECT_ADD_FUSION_CODE:case EFFECT_ADD_FUSION_SETCODE:case EFFECT_CHANGE_FUSION_ATTRIBUTE:case EFFECT_ADD_LINK_CODE:case EFFECT_ADD_LINK_ATTRIBUTE:case EFFECT_ADD_LINK_RACE:case EFFECT_SET_BATTLE_ATTACK:case EFFECT_SET_BATTLE_DEFENSE:case EFFECT_TUNER:case EFFECT_XYZ_MIN_COUNT:return true;default:return false;}}
#include "editor_summon_history.inc"
#include "editor_attachments.inc"
void FilterStates(){uint32_t type=EffectiveType();bool monster=type&TYPE_MONSTER;int rows[2]{};std::vector<int> visible;for(int i=0;i<32;++i){bool show=type!=0;bool combat=(i>=3 && i<=8)||i==10||i==12||i==26||i>=28;show &= !combat || monster; if(i==24 || i==25)show &= !!(type&TYPE_TRAP);if(i==27)show &= !!(type&TYPE_DUAL);if((i==6 || i==7) && (type&TYPE_LINK))show=false;if(i==7 && (type&TYPE_TOKEN))show=false;ruleBoxes[i]->setVisible(show);if(show)visible.push_back(i);}size_t half=(visible.size()+1)/2;for(size_t n=0;n<visible.size();++n){int i=visible[n],col=n>=half,row=int(col?n-half:n);ruleBoxes[i]->setRelativePosition({15+col*295,10+row*25,300+col*295,34+row*25});}
 for(int i=0;i<4;++i)stateBoxes[i]->setVisible(i==3 || (i==0 && monster));stateBoxes[0]->setVisible(monster && location->getItemData(location->getSelected())==LOCATION_MZONE && (position->getItemData(position->getSelected())&POS_FACEDOWN));if(type&TYPE_LINK)stateBoxes[2]->setVisible(false);
 uint32_t previous=extraState->getItemData(extraState->getSelected());extraState->clear();for(size_t i=0;i<sizeof(editor_catalog_codes)/sizeof(editor_catalog_codes[0]);++i){uint32_t c=editor_catalog_codes[i];bool duplicate=std::find(std::begin(editor_rule_codes),std::end(editor_rule_codes),c)!=std::end(editor_rule_codes) || c==EFFECT_FORBIDDEN;if((c==EFFECT_CANNOT_SSET) && !(type&(TYPE_SPELL|TYPE_TRAP)))continue;if(duplicate || !type || (MonsterEffect(c) && !monster))continue;if((c==EFFECT_UPDATE_RANK || c==EFFECT_CHANGE_RANK || c==EFFECT_XYZ_MIN_COUNT) && !(type&TYPE_XYZ))continue;if((c==EFFECT_UPDATE_LEVEL || c==EFFECT_CHANGE_LEVEL || c==EFFECT_XYZ_LEVEL) && (type&(TYPE_XYZ|TYPE_LINK)))continue;if(c>=EFFECT_UPDATE_LSCALE && c<=EFFECT_CHANGE_RSCALE && !(type&TYPE_PENDULUM))continue;if((c==EFFECT_DUAL_SUMMONABLE) && !(type&TYPE_DUAL))continue;if((c==EFFECT_CANNOT_SUMMON || c==EFFECT_CANNOT_MSET || c==EFFECT_EXTRA_SUMMON_COUNT || c==EFFECT_EXTRA_SET_COUNT || c==EFFECT_DECREASE_TRIBUTE || c==EFFECT_DECREASE_TRIBUTE_SET || c==EFFECT_TRIBUTE_LIMIT) && !editor_allows_normal_summon(type))continue;if(c==EFFECT_CANNOT_FLIP_SUMMON && (type&(TYPE_LINK|TYPE_TOKEN)))continue;if(c==EFFECT_NONTUNER && !(type&TYPE_TUNER))continue;if(c==EFFECT_TUNER && (type&TYPE_TUNER))continue;if((type&TYPE_LINK) && (c==EFFECT_DEFENSE_ATTACK || c==EFFECT_UPDATE_DEFENSE || c==EFFECT_SET_DEFENSE || c==EFFECT_SET_DEFENSE_FINAL || c==EFFECT_SET_BASE_DEFENSE || c==EFFECT_SET_BASE_DEFENSE_FINAL || c==EFFECT_SWAP_AD || c==EFFECT_SWAP_BASE_AD || c==EFFECT_SET_BATTLE_DEFENSE))continue;extraState->addItem(editor_catalog_labels[i],c);}ComboValue(extraState,previous);
}
void FilterNames(uint32_t chosen){std::wstring term=nameSearch->getText();nameBox->clear();nameBox->addItem(L"原本卡名",0);bool retained=chosen==0;size_t count=0;for(auto code:nameCodes){if(!term.empty() && !dataManager.CardNameContains(dataManager.GetName(code),term.c_str()) && std::to_wstring(code).find(term)==std::wstring::npos)continue;std::wstring row=dataManager.GetName(code);row+=L" ["+std::to_wstring(code)+L"]";nameBox->addItem(row.c_str(),code);retained|=chosen==code;++count;}if(!retained){std::wstring row=L"当前：";row+=dataManager.GetName(chosen);row+=L" ["+std::to_wstring(chosen)+L"]";nameBox->addItem(row.c_str(),chosen);}ComboValue(nameBox,chosen);nameSearch->setToolTipText((L"输入卡名关键词或卡号，实时筛选右侧卡名库。当前匹配 "+std::to_wstring(count)+L" 张卡；搜索不会改变已选卡名。").c_str());}
std::wstring nonLinkLevel=L"4";bool showingLinkRating=false;
void ResetSummonHistory(const editor_card_info* card);void UpdateSummonHistoryUI();
void FillForm(const editor_card_info* card){
 ResetSummonHistory(card);
 attachmentReady=false;draftMaterials.clear();draftCounters.clear();
 specialOrigin->setChecked(card && card->summon_method==1);normalOrigin->setChecked(card && card->summon_method==2);flipOrigin->setChecked(card && card->summon_method==3);
 ComboValue(cardForm,card?(card->form==6?3:card->form):0);CardData data{};if(card)dataManager.GetData(card->code,&data);
 ComboValue(raceBox,card?card->race:1);ComboValue(attributeBox,card?card->attribute:1);
 attackBox->setText(std::to_wstring(card && card->attack<=0x7fffffffU?card->attack:0).c_str());defenseBox->setText(std::to_wstring(card && card->defense<=0x7fffffffU?card->defense:0).c_str());levelBox->setText(std::to_wstring(card && card->level?card->level:4).c_str());
 uint32_t type=card?card->effective_type:TYPE_EFFECT;showingLinkRating=card && (card->form==3 || card->form==5 || card->form==6) && (type&TYPE_LINK);nonLinkLevel=showingLinkRating?L"4":levelBox->getText();effectMonster->setChecked(!card || (type&TYPE_EFFECT));for(int i=0;i<13;++i)if(typeBoxes[i])typeBoxes[i]->setChecked(type&typeMasks[i]);ComboValue(kindBox,type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK|TYPE_RITUAL));nameSearch->setText(L"");FilterNames(card && card->form>=5?card->self_summon:0);for(int i=0;i<8;++i)arrowBoxes[i]->setChecked(card && (card->link_marker&arrowMasks[i]));
 keepSpellTrap->setChecked(card && (card->form==6?(card->monster_flags&0x80000000U):(card->form==3 && (card->monster_flags&2))));selfSummon->setChecked(card && card->form==4 && card->self_summon);
 for(int i=0;i<32;++i)ruleBoxes[i]->setChecked(card && (card->restrictions&(1U<<i)));for(int i=0;i<4;++i)stateBoxes[i]->setChecked((i==0 || i==3) && card && (card->states&stateMasks[i]));RefreshCustom();
}
void UpdateForm(){
 CardData data{};bool found=CurrentData(data);uint32_t chosen=Shape();cardForm->clear();cardForm->addItem(L"原本卡片形态",0);if(found && (data.type&TYPE_MONSTER)){cardForm->addItem(L"自定义怪兽属性／类型／卡名",5);cardForm->addItem(L"怪兽作为永续魔法",1);cardForm->addItem(L"怪兽作为永续陷阱",2);}if(found && (data.type&(TYPE_SPELL|TYPE_TRAP)))cardForm->addItem(L"魔法／陷阱作为自定义怪兽",3);if(found && (data.type&TYPE_TRAP) && TrapProfile(data.code))cardForm->addItem(L"自身可变怪兽的陷阱（沿用原设定）",4);ComboValue(cardForm,chosen);
 int f=Shape();if(f==4){const auto& p=trapProfiles[data.code];attackBox->setText(std::to_wstring(p.attack).c_str());defenseBox->setText(std::to_wstring(p.defense).c_str());levelBox->setText(std::to_wstring(p.level).c_str());ComboValue(raceBox,p.race);ComboValue(attributeBox,p.attribute);}
 bool custom=f==3 || f==5;uint32_t type=EffectiveType();raceBox->setEnabled(custom);attributeBox->setEnabled(custom);attackBox->setEnabled(custom);defenseBox->setEnabled(custom && !(type&TYPE_LINK));levelBox->setEnabled(custom && !(type&TYPE_LINK));levelLabel->setText(type&TYPE_XYZ?L"阶级":type&TYPE_LINK?L"连接数（按箭头）":L"等级");effectMonster->setVisible(custom);effectMonster->setEnabled(custom);keepSpellTrap->setVisible(f==3);keepSpellTrap->setEnabled(f==3);selfSummon->setVisible(f==4);selfSummon->setEnabled(f==4);if(f!=4)selfSummon->setChecked(false);for(auto* box:typeBoxes)if(box)box->setEnabled(custom);kindBox->setVisible(custom);kindBox->setEnabled(custom);kindLabel->setVisible(custom);otherTypesButton->setVisible(custom);otherTypesButton->setEnabled(custom);nameBox->setEnabled(custom);nameBox->setVisible(custom);nameLabel->setVisible(custom);nameSearch->setVisible(custom);nameSearch->setEnabled(custom);bool arrows=custom && (type&TYPE_LINK);if(arrows){if(!showingLinkRating)nonLinkLevel=levelBox->getText();unsigned count=0;for(auto* box:arrowBoxes)if(box->isChecked())++count;levelBox->setText(std::to_wstring(count).c_str());}else if(showingLinkRating){levelBox->setText(nonLinkLevel.c_str());for(auto* box:arrowBoxes)box->setChecked(false);}showingLinkRating=arrows;arrowsLabel->setVisible(arrows);for(auto* box:arrowBoxes){box->setVisible(arrows);box->setEnabled(arrows);}if(counterOpen){bool monster=type&TYPE_MONSTER;counterOpen->setVisible(monster);materialOpen->setVisible(monster&&(type&TYPE_XYZ));int y=custom?(arrows?645:525):365;counterOpen->setRelativePosition({15,y,585,y+30});materialOpen->setRelativePosition({15,y+40,585,y+70});}formScroll->setMax(custom?(arrows?265:165):0);formScroll->setVisible(formScroll->getMax()>0);formScroll->setPos(std::min(formScroll->getPos(),formScroll->getMax()));int scroll=formScroll->getPos();formContent->setRelativePosition({0,-scroll,590,715-scroll});FilterStates();UpdateSummonHistoryUI();
}
uint32_t summonChoiceCode=~0U,summonChoiceZone=~0U,summonChoicePosition=~0U,summonChoiceType=~0U;int summonChoiceForm=-1;
bool IsTrapMonster(uint32_t type){CardData data{};return CurrentData(data) && (data.type&TYPE_TRAP) && (type&TYPE_MONSTER);}
bool CanUseExtraMonsterZone(uint32_t type){return !IsTrapMonster(type) && (type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK|TYPE_PENDULUM)) && specialOrigin->isChecked() && summonSource->getItemData(summonSource->getSelected())==LOCATION_EXTRA;}
void RefreshRegions(uint32_t type){
 uint32_t chosen=location->getItemData(location->getSelected());location->clear();
 const std::pair<const wchar_t*,uint32_t> zones[]={{L"手牌",LOCATION_HAND},{L"怪兽区 1～7",LOCATION_MZONE},{L"魔陷区 1～5",LOCATION_SZONE},{L"场地区",LOCATION_FZONE},{L"灵摆区 1～2",LOCATION_PZONE},{L"墓地",LOCATION_GRAVE},{L"除外",LOCATION_REMOVED},{L"额外卡组 1～3",LOCATION_EXTRA},{L"卡组 1～3",LOCATION_DECK}};
 bool fieldMonster=Shape()==3 || Shape()==4 || Shape()==5 || IsTrapMonster(type);for(const auto& zone:zones){if(fieldMonster && zone.second!=LOCATION_MZONE)continue;if((Shape()==1 || Shape()==2) && zone.second!=LOCATION_SZONE)continue;if(zone.second==LOCATION_MZONE && !(type&TYPE_MONSTER))continue;if((zone.second==LOCATION_HAND || zone.second==LOCATION_DECK) && (type&TYPE_MONSTER) && (type&TYPES_EXTRA_DECK))continue;if(zone.second==LOCATION_FZONE && (type&(TYPE_SPELL|TYPE_FIELD))!=(TYPE_SPELL|TYPE_FIELD))continue;
 if(zone.second==LOCATION_SZONE && (!(type&(TYPE_SPELL|TYPE_TRAP)) || (type&(TYPE_SPELL|TYPE_FIELD))==(TYPE_SPELL|TYPE_FIELD)))continue;
 if(zone.second==LOCATION_EXTRA && (!(type&TYPE_MONSTER) || !(type&(TYPES_EXTRA_DECK|TYPE_PENDULUM))))continue;
 if(zone.second==LOCATION_PZONE && ((type&(TYPE_PENDULUM|TYPE_MONSTER))!=(TYPE_PENDULUM|TYPE_MONSTER)))continue;location->addItem(zone.second==LOCATION_MZONE && !CanUseExtraMonsterZone(type)?L"怪兽区 1～5":zone.first,zone.second);}
 ComboValue(location,chosen);
}
void UpdateSummonChoices(){
 if(!summonTurn)return;uint32_t previous=summonTurn->getItemData(summonTurn->getSelected());uint32_t type=EffectiveType();RefreshRegions(type);bool zoneChanged=summonChoiceZone!=location->getItemData(location->getSelected());int index=results->getSelected();summonChoiceCode=index>=0 && index<int(codes.size())?codes[index]:0;summonChoiceZone=location->getItemData(location->getSelected());summonChoiceForm=Shape();
 bool normalAllowed=editor_allows_normal_summon(type) && !IsTrapMonster(type);if(!normalAllowed)normalOrigin->setChecked(false);
 uint32_t oldSource=summonSource->getItemData(summonSource->getSelected());bool ownSpecial=Shape()==4 && selfSummon->isChecked();if(ownSpecial){specialOrigin->setChecked(true);normalOrigin->setChecked(false);flipOrigin->setChecked(false);oldSource=LOCATION_SZONE;}if(sourceChoiceCode!=summonChoiceCode){oldSource=(type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK))?LOCATION_EXTRA:0;sourceChoiceCode=summonChoiceCode;if(ownSpecial)oldSource=LOCATION_SZONE;}summonSource->clear();const std::pair<const wchar_t*,uint32_t> sources[]={{L"手牌",LOCATION_HAND},{L"墓地",LOCATION_GRAVE},{L"除外",LOCATION_REMOVED},{L"卡组",LOCATION_DECK},{L"魔法与陷阱区",LOCATION_SZONE},{L"额外卡组",LOCATION_EXTRA}};for(const auto& source:sources)if(!((type&TYPES_EXTRA_DECK) && (source.second==LOCATION_HAND || source.second==LOCATION_DECK)))if(source.second!=LOCATION_EXTRA || (!IsTrapMonster(type) && (type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK|TYPE_PENDULUM))))summonSource->addItem(source.first,source.second);ComboValue(summonSource,oldSource);
 uint32_t pos=position->getItemData(position->getSelected());position->clear();bool monster=type&TYPE_MONSTER;uint32_t zone=summonChoiceZone;if(zoneChanged)pos=(zone==LOCATION_HAND || zone==LOCATION_DECK || zone==LOCATION_EXTRA)?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK;
 if(zone==LOCATION_MZONE && monster){position->addItem(L"表侧攻击",POS_FACEUP_ATTACK);if(!(type&TYPE_LINK))position->addItem(L"表侧守备",POS_FACEUP_DEFENSE);if(!(type&(TYPE_LINK|TYPE_TOKEN)))position->addItem(L"里侧守备",POS_FACEDOWN_DEFENSE);}
 else if(zone==LOCATION_SZONE || zone==LOCATION_FZONE){if(type&(TYPE_CONTINUOUS|TYPE_FIELD))position->addItem(L"表侧放置",POS_FACEUP_ATTACK);position->addItem(L"盖放",POS_FACEDOWN_DEFENSE);}
 else if(zone==LOCATION_DECK || zone==LOCATION_HAND)position->addItem(L"里侧",POS_FACEDOWN_DEFENSE);
 else if(zone==LOCATION_EXTRA){if(!(type&TYPE_PENDULUM) || (type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ)))position->addItem(L"里侧",POS_FACEDOWN_DEFENSE);if(type&TYPE_PENDULUM)position->addItem(L"表侧",POS_FACEUP_ATTACK);}
 else {position->addItem(L"表侧",POS_FACEUP_ATTACK);if(zone==LOCATION_REMOVED)position->addItem(L"里侧",POS_FACEDOWN_DEFENSE);}
 ComboValue(position,pos);pos=position->getItemData(position->getSelected());summonChoicePosition=pos;summonChoiceType=type;
 bool backrow=(type&(TYPE_SPELL|TYPE_TRAP)) && (zone==LOCATION_SZONE || zone==LOCATION_FZONE);bool grave=zone==LOCATION_GRAVE;historyLabel->setText(grave?L"是否当回合送去墓地":backrow?L"是否当回合盖放":L"是否当回合召唤");summonTurn->clear();summonTurn->addItem(L"否",0);
 if(grave)summonTurn->addItem(L"是",EDITOR_SENT_GRAVE_TURN);else if(backrow)summonTurn->addItem(L"是",STATUS_SET_TURN);else if(monster && zone==LOCATION_MZONE)summonTurn->addItem(L"是",1);
 ComboValue(summonTurn,previous);summonTurn->setVisible(grave || backrow || (monster && zone==LOCATION_MZONE));historyLabel->setVisible(grave || backrow || (monster && zone==LOCATION_MZONE));summonTurn->setEnabled(grave || backrow || (monster && zone==LOCATION_MZONE));bool hasReviveLimit=monster && (type&(TYPE_RITUAL|TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK|TYPE_SPSUMMON));procedure->setVisible(hasReviveLimit);if(!hasReviveLimit)procedure->setChecked(false);bool canFlip=monster && zone==LOCATION_MZONE && !(type&(TYPE_LINK|TYPE_TOKEN)) && (pos&POS_FACEUP);if(!canFlip)flipOrigin->setChecked(false);flipOrigin->setVisible(canFlip);if(monster && zone==LOCATION_MZONE && !specialOrigin->isChecked() && !normalOrigin->isChecked() && !flipOrigin->isChecked()){if(normalAllowed)normalOrigin->setChecked(true);else specialOrigin->setChecked(true);}RefreshRegions(type);specialOrigin->setVisible(monster && zone==LOCATION_MZONE);normalOrigin->setVisible(monster && zone==LOCATION_MZONE && normalAllowed);summonSource->setVisible(monster && zone==LOCATION_MZONE && specialOrigin->isChecked());summonSourceLabel->setVisible(monster && zone==LOCATION_MZONE && specialOrigin->isChecked());summonSource->setEnabled(monster && zone==LOCATION_MZONE);bool numbered=zone==LOCATION_MZONE || zone==LOCATION_SZONE || zone==LOCATION_PZONE || zone==LOCATION_DECK || zone==LOCATION_EXTRA;slot->setVisible(numbered);slotLabel->setVisible(numbered);slotExplanation=zone==LOCATION_MZONE?(CanUseExtraMonsterZone(type)?L"1～5：主要怪兽区的位置\n6、7：额外怪兽区，仅限从额外卡组特殊召唤":L"1～5：主要怪兽区的位置"):zone==LOCATION_SZONE?L"1～5：魔法与陷阱区的位置":zone==LOCATION_PZONE?L"1：左侧灵摆区域\n2：右侧灵摆区域":zone==LOCATION_EXTRA?L"1：插入当前表示分组的顶端\n2：插入当前表示分组的底端\n3：随机插入中间位置，保留其他卡片顺序":L"1：牌顶\n2：牌底\n3：随机插入，保留其他卡片顺序";slot->setToolTipText(L"");if(slotTooltip)slotTooltip->setText(slotExplanation.c_str());FilterStates();
 UpdateSummonHistoryUI();
}
void Search() {
 codes.clear();results->clear();FillForm(nullptr);
 std::wstring term=search->getText();
 if(term.empty()) {status->setText(L"请输入卡名或卡号");return;}
 wchar_t* end=nullptr;
 auto id=std::wcstoull(term.c_str(),&end,10);
 const bool numeric=end && *end==0;
 for(const auto& entry:dataManager.GetStringTable()) {
  if((numeric && entry.first==id) || (!numeric && dataManager.CardNameContains(entry.second.name.c_str(),term.c_str()))) codes.push_back(entry.first);
 }
 std::sort(codes.begin(),codes.end());
 if(codes.size()>200)codes.resize(200);
 for(auto code:codes) {
  std::wstring name=std::to_wstring(code)+L"  "+dataManager.GetName(code);
  results->addItem(name.c_str());
 }
 if(!codes.empty()){results->setSelected(0);mainGame->ShowCardInfo(codes[0]);CardData d{};dataManager.GetData(codes[0],&d);ComboValue(raceBox,d.race);ComboValue(attributeBox,d.attribute);attackBox->setText(std::to_wstring(std::max(0,d.attack)).c_str());defenseBox->setText(std::to_wstring(std::max(0,d.defense)).c_str());levelBox->setText(std::to_wstring(d.level?d.level:4).c_str());for(int i=0;i<8;++i)arrowBoxes[i]->setChecked(d.link_marker&arrowMasks[i]);effectMonster->setChecked(!(d.type&TYPE_MONSTER) || (d.type&TYPE_EFFECT));for(int j=0;j<13;++j)if(typeBoxes[j])typeBoxes[j]->setChecked((d.type&TYPE_MONSTER) && (d.type&typeMasks[j]));ComboValue(kindBox,(d.type&TYPE_MONSTER)?d.type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK|TYPE_RITUAL):0);}
 UpdateForm();UpdateSummonChoices();
 status->setText(codes.empty()?L"没有找到卡片":L"");
}
bool Number(irr::gui::IGUIEditBox* box, uint32_t& out) {
 const wchar_t* text=box->getText();
 if(!*text)return false;
 for(auto p=text;*p;++p)if(*p<L'0'||*p>L'9')return false;
 wchar_t* end=nullptr;auto value=std::wcstoull(text,&end,10);
 if(*end || value>std::numeric_limits<uint32_t>::max())return false;
 out=uint32_t(value);return true;
}
void Submit(bool isLP,bool deletion=false) {
 if(!available)return;
 editor_command cmd{};cmd.revision=revision;cmd.player=player->getSelected();
 cmd.owner=ownerOther->isChecked()?1-cmd.player:cmd.player;
 if(isLP) {
  cmd.opcode=2;
  if(!Number(lp,cmd.value)||!cmd.value||cmd.value>0x7fffffffU){status->setText(L"LP 必须是 1～2147483647 的整数");return;}
 } else if(deletion) {
  if(!SelectedCard()){status->setText(L"请先选择要删除的卡片");return;}
  cmd.opcode=3;cmd.target_low=uint32_t(selectedID);cmd.target_high=uint32_t(selectedID>>32);
 } else {
  if(mode->getSelected()==1 && !SelectedCard()){status->setText(L"请先选择已有卡片");return;}
  int index=results->getSelected();
  if(index<0 || index>=int(codes.size())){status->setText(L"请先搜索并选择卡片");return;}
  cmd.opcode=mode->getSelected()==1?4:1;cmd.code=codes[index];
  if(cmd.opcode==4){cmd.target_low=uint32_t(selectedID);cmd.target_high=uint32_t(selectedID>>32);}
  cmd.location=location->getItemData(location->getSelected());
  uint32_t value=0;
  uint32_t limit=cmd.location==LOCATION_MZONE?(CanUseExtraMonsterZone(EffectiveType())?7:5):cmd.location==LOCATION_SZONE?5:cmd.location==LOCATION_PZONE?2:3;
  if(slot->isVisible() && (!Number(slot,value)||!value||value>limit)){status->setText(cmd.location==LOCATION_MZONE && value>5?L"6、7 号位仅限从额外卡组特殊召唤的怪兽":L"位置编号超出当前区域的范围");return;}
  cmd.sequence=slot->isVisible()?value-1:0;
  cmd.form=Shape();cmd.race=raceBox->getItemData(raceBox->getSelected());cmd.attribute=attributeBox->getItemData(attributeBox->getSelected());
  if(!Number(attackBox,cmd.attack)||!Number(defenseBox,cmd.defense)||!Number(levelBox,cmd.level)){status->setText(L"攻击力、守备力和等级必须为整数");return;}
  cmd.monster_flags=(effectMonster->isChecked()?1:0)|(keepSpellTrap->isChecked()?2:0);cmd.self_summon=selfSummon->isChecked()?1:0;
  if(cmd.form==3 || cmd.form==5){if(cmd.form==3){cmd.form=6;}cmd.monster_flags=CustomType()|((cmd.form==6 && keepSpellTrap->isChecked())?0x80000000U:0);cmd.self_summon=nameBox->getItemData(nameBox->getSelected());if(cmd.monster_flags&TYPE_LINK){cmd.defense=0;for(int i=0;i<8;++i)if(arrowBoxes[i]->isChecked())cmd.defense|=arrowMasks[i];if(!cmd.defense){status->setText(L"请至少选择一个连接箭头");return;}}}
  for(int i=0;i<4;++i)if((i==0 || i==3) && stateBoxes[i]->isChecked())cmd.states|=stateMasks[i];for(int i=0;i<32;++i)if(ruleBoxes[i]->isChecked() && ruleBoxes[i]->isVisible())cmd.restrictions|=1U<<i;
  cmd.position=position->getItemData(position->getSelected());
  if(cmd.location==LOCATION_SZONE || cmd.location==LOCATION_FZONE){cmd.states&=~STATUS_SET_TURN;cmd.states|=summonTurn->getItemData(summonTurn->getSelected())&STATUS_SET_TURN;}
  uint32_t turnHistory=summonTurn->getItemData(summonTurn->getSelected());if(cmd.location==LOCATION_MZONE && (int(specialOrigin->isChecked())+int(normalOrigin->isChecked())+int(flipOrigin->isChecked())!=1)){status->setText(L"请选择一种召唤方式");return;}if(cmd.location==LOCATION_MZONE && turnHistory){if(!specialOrigin->isChecked() && !normalOrigin->isChecked() && !flipOrigin->isChecked()){status->setText(L"请先选择召唤方式");return;}turnHistory=specialOrigin->isChecked()?EDITOR_SUMMON_SPECIAL:normalOrigin->isChecked()?EDITOR_SUMMON_NORMAL:EDITOR_SUMMON_FLIP;}cmd.value=(procedure->isChecked()?EDITOR_PROC_COMPLETE:0)|((cmd.location==LOCATION_MZONE || cmd.location==LOCATION_GRAVE)?turnHistory:0);if(cmd.location==LOCATION_MZONE){if(specialOrigin->isChecked())cmd.value|=EDITOR_SPECIAL_ORIGIN|(summonSource->getItemData(summonSource->getSelected())<<8);else if(normalOrigin->isChecked())cmd.value|=EDITOR_NORMAL_ORIGIN|(LOCATION_HAND<<8);else if(flipOrigin->isChecked())cmd.value|=EDITOR_FLIP_ORIGIN|(LOCATION_MZONE<<8);}
 }
 if(QueueWithAttachments(cmd)) {
  available=false;create->setEnabled(false);setLP->setEnabled(false);deleteButton->setEnabled(false);
  status->setText(L"正在应用修改并重新计算合法操作…");
 } else status->setText(L"当前有操作正在处理，请稍后重试");
}

void FillExisting() {
 auto* card=SelectedCard();
 if(!card){create->setEnabled(false);deleteButton->setEnabled(false);return;}
 codes={card->code};results->clear();results->addItem(dataManager.GetName(card->code));results->setSelected(0);UpdateForm();FillForm(card);UpdateForm();UpdateSummonChoices();
 player->setSelected(card->player);formPlayer=card->player;ownerOther->setChecked(card->owner!=card->player);
 uint32_t zone=card->location,index=card->sequence;
 if(zone==LOCATION_SZONE && card->pzone){zone=LOCATION_PZONE;index=card->sequence==0 || card->sequence==6?0:1;}
 else if(zone==LOCATION_SZONE && card->sequence==5){zone=LOCATION_FZONE;index=0;}
 if(zone==LOCATION_DECK || zone==LOCATION_EXTRA)index=0;
 for(unsigned int i=0;i<location->getItemCount();++i)if(location->getItemData(i)==zone)location->setSelected(i);
 slot->setText(std::to_wstring(index+1).c_str());
 for(unsigned int i=0;i<position->getItemCount();++i)if(position->getItemData(i)==card->position)position->setSelected(i);
 procedure->setChecked(!!(card->flags&EDITOR_PROC_COMPLETE));UpdateSummonChoices();ComboValue(position,card->position);specialOrigin->setChecked(card->summon_method==1);normalOrigin->setChecked(card->summon_method==2);flipOrigin->setChecked(card->summon_method==3);UpdateSummonChoices();
 ComboValue(summonTurn,card->location==LOCATION_SZONE?(card->states&STATUS_SET_TURN):(card->location==LOCATION_GRAVE?(card->flags&EDITOR_SENT_GRAVE_TURN):((card->flags&14U)?1U:0U)));
 ComboValue(summonSource,card->summon_source);UpdateSummonChoices();create->setEnabled(available && card->location!=LOCATION_OVERLAY);deleteButton->setEnabled(available);
 status->setText(card->location==LOCATION_OVERLAY?L"超量素材可删除；其他属性暂不支持直接修改":L"正在编辑这张卡；修改后点击保存，或直接删除");
}
void SetMode() {
 bool editing=mode->getSelected()==1;
 search->setVisible(!editing);searchLabel->setVisible(!editing);searchButton->setVisible(!editing);results->setVisible(!editing);existing->setVisible(editing);
 deleteButton->setVisible(editing);deleteButton->setEnabled(editing && available && SelectedCard());
 create->setText(editing?L"保存修改":L"生成卡片");
 if(editing)FillExisting();else {selectedID=0;existing->setSelected(-1);create->setEnabled(available);UpdateSummonChoices();}
}
}
std::wstring DuelEditor::PlayerName(uint32_t player){std::lock_guard<std::recursive_mutex> lock(editorGuiMutex);return playerNames[player%2];}
void DuelEditor::RefreshPlayerNames(){BufferIO::CopyWideString(playerNames[CurrentView()].c_str(),mainGame->dInfo.hostname);BufferIO::CopyWideString(playerNames[1-CurrentView()].c_str(),mainGame->dInfo.clientname);}
uint32_t DuelEditor::CurrentView(){return viewPlayer.load();}
uint32_t DuelEditor::TimingMode(uint32_t player){return player<2?timingModes[player].load():0;}
void DuelEditor::CaptureTiming(){if(active)timingModes[CurrentView()]=(mainGame->ignore_chain?1U:0U)|(mainGame->always_chain?2U:0U)|(mainGame->chain_when_avail?4U:0U);}
void DuelEditor::SetPromptPlayer(int player){promptPlayer=player;}
void DuelEditor::MaskPendingPrompt(){if(promptPlayer>=2 || promptPlayer==int(CurrentView()))return;std::lock_guard<std::mutex> lock(mainGame->gMutex);mainGame->HideElement(mainGame->wQuery);mainGame->HideElement(mainGame->wOptions);mainGame->HideElement(mainGame->wPosSelect);mainGame->HideElement(mainGame->wCardSelect);bool promptFocus=false;for(auto* e=mainGame->env->getFocus();e;e=e->getParent())if(e==mainGame->wQuery || e==mainGame->wOptions || e==mainGame->wPosSelect || e==mainGame->wCardSelect)promptFocus=true;if(promptFocus)mainGame->env->setFocus(nullptr);std::wstring text=L"等待玩家 "+std::to_wstring(promptPlayer+1)+L" 处理当前时点；切换到该玩家后选择。";mainGame->stHintMsg->setText(text.c_str());mainGame->stHintMsg->setVisible(true);}
void DuelEditor::ApplyViewSwitch(){std::lock_guard<std::mutex> lock(mainGame->gMutex);std::lock_guard<std::recursive_mutex> uiLock(editorGuiMutex);CaptureTiming();bool replaySwapped=mainGame->dInfo.isReplaySwapped;mainGame->dField.ReplaySwap();mainGame->dInfo.isReplaySwapped=replaySwapped;mainGame->dField.selectable_field=(mainGame->dField.selectable_field>>16)|(mainGame->dField.selectable_field<<16);mainGame->dField.selected_field=(mainGame->dField.selected_field>>16)|(mainGame->dField.selected_field<<16);mainGame->dField.hovered_card=nullptr;mainGame->dField.HideMenu();mainGame->stHintMsg->setVisible(false);viewPlayer=1-CurrentView();uint32_t timing=TimingMode(CurrentView());mainGame->ignore_chain=timing&1;mainGame->always_chain=timing&2;mainGame->chain_when_avail=timing&4;mainGame->dField.UpdateChainButtons();if(mode->getSelected()==0){player->setSelected(CurrentView());formPlayer=CurrentView();}orderPlayer->setSelected(CurrentView());RefreshOrder();}
void DuelEditor::Initialize() {
 auto* env=mainGame->env;
 toggle=env->addButton({310,5,455,35},nullptr,TOGGLE,L"进入决构编辑");toggle->setVisible(false);
 panel=env->addWindow({330,35,740,615},false,L"决构编辑器 · v0.1.17");
 panel->getCloseButton()->setVisible(false);panel->setVisible(false);
 mode=env->addComboBox({15,30,395,58},panel,MODE);mode->addItem(L"生成新卡片",0);mode->addItem(L"编辑已有卡片",1);
 searchLabel=env->addStaticText(L"卡名或卡号",{15,62,120,81},false,false,panel);
 search=env->addEditBox(L"",{15,83,285,110},true,panel,9008);
 searchButton=env->addButton({295,83,395,110},panel,SEARCH,L"搜索");
 results=env->addListBox({15,116,395,225},panel,RESULTS,true);results->setItemHeight(22);
 existing=env->addListBox({15,65,395,225},panel,EXISTING,true);existing->setItemHeight(22);existing->setVisible(false);
 env->addStaticText(L"所在方／控制者",{15,237,115,258},false,false,panel);
 player=env->addComboBox({120,233,225,260},panel,9011);player->addItem(L"玩家 1",0);player->addItem(L"玩家 2",1);
 ownerOther=env->addCheckBox(false,{235,234,395,260},panel,9018,L"拥有者为另一方");
 location=env->addComboBox({15,273,170,300},panel,REGION);
 const std::pair<const wchar_t*,uint32_t> zones[]={ {L"手牌",LOCATION_HAND},{L"怪兽区 1～7",LOCATION_MZONE},{L"魔陷区 1～5",LOCATION_SZONE},{L"场地区",LOCATION_FZONE},{L"灵摆区 1～2",LOCATION_PZONE},{L"墓地",LOCATION_GRAVE},{L"除外",LOCATION_REMOVED},{L"额外卡组 1～3",LOCATION_EXTRA},{L"卡组 1～3",LOCATION_DECK} };
 for(const auto& zone:zones)location->addItem(zone.first,zone.second);
 slotLabel=env->addStaticText(L"位置",{185,278,230,300},false,false,panel);
 slot=env->addEditBox(L"1",{230,273,280,300},true,panel,9014);
 position=env->addComboBox({290,273,395,300},panel,9019);
 position->addItem(L"表侧攻击",POS_FACEUP_ATTACK);position->addItem(L"表侧守备",POS_FACEUP_DEFENSE);position->addItem(L"里侧守备",POS_FACEDOWN_DEFENSE);
 specialOrigin=env->addCheckBox(false,{15,311,140,337},panel,9772,L"是否特殊召唤");normalOrigin=env->addCheckBox(false,{145,311,270,337},panel,9773,L"是否通常召唤");flipOrigin=env->addCheckBox(false,{275,311,395,337},panel,9774,L"是否反转召唤");
 procedure=env->addCheckBox(false,{15,405,220,427},panel,9775,L"已完成正规召唤");
 historyLabel=env->addStaticText(L"是否当回合召唤",{15,373,155,400},false,false,panel);
 summonTurn=env->addComboBox({165,370,395,397},panel,9013);
 summonTurn->addItem(L"否（此前已在场）",0);
 summonTurn->addItem(L"是 · 通常召唤",EDITOR_SUMMON_NORMAL);
 summonTurn->addItem(L"是 · 特殊召唤",EDITOR_SUMMON_SPECIAL);
 summonTurn->addItem(L"是 · 反转召唤",EDITOR_SUMMON_FLIP);
 summonTurn->setEnabled(false);
 summonSourceLabel=env->addStaticText(L"特殊召唤来源",{15,343,155,369},false,false,panel,9770);summonSource=env->addComboBox({165,340,395,367},panel,9771);summonSource->addItem(L"未指定",0);
 create=env->addButton({15,531,135,562},panel,CREATE,L"生成卡片");
 slotHelp=env->addStaticText(L"",{15,376,395,400},false,false,panel);slotHelp->setVisible(false);
 env->addStaticText(L"目标玩家 LP",{15,435,125,456},false,false,panel);
 lp=env->addEditBox(L"8000",{130,430,245,457},true,panel,9010);
 setLP=env->addButton({255,430,395,457},panel,LP,L"修改 LP");
 status=env->addStaticText(L"人工放置不会作为正常召唤或送墓操作",{15,460,395,484},false,true,panel,9020);
 env->addButton({275,531,395,562},panel,CONTINUE,L"继续对局");
 deleteButton=env->addButton({145,531,265,562},panel,DELETE_CARD,L"删除卡片");deleteButton->setVisible(false);
 env->addButton({15,490,205,517},panel,9012,L"卡片形态／状态与限制…");
 env->addButton({310,490,395,517},panel,9520,L"抽卡…");drawWindow=env->addWindow({375,210,725,385},false,L"立即抽卡",nullptr,9524);drawWindow->getCloseButton()->setVisible(false);drawWindow->setVisible(false);env->addStaticText(L"抽卡数量",{15,40,115,70},false,false,drawWindow);drawCount=env->addEditBox(L"1",{125,35,330,70},true,drawWindow,9521);drawStatus=env->addStaticText(L"",{15,80,330,110},false,true,drawWindow,9525);env->addButton({35,125,155,155},drawWindow,9522,L"立即抽卡");env->addButton({195,125,315,155},drawWindow,9523,L"取消");

 undo=env->addButton({820,590,860,630},nullptr,UNDO,L"");undo->setToolTipText(L"撤回一个时点");undo->setVisible(false);
 pauseButton=env->addButton({867,590,912,630},nullptr,9401,L"");pauseButton->setToolTipText(L"暂停／继续（空格）");pauseButton->setVisible(false);
 stepButton=env->addButton({919,590,959,630},nullptr,9402,L"");stepButton->setToolTipText(L"前进一个时点");stepButton->setVisible(false);
 saveButton=env->addButton({966,590,1020,630},nullptr,9403,L"存档");saveButton->setVisible(false);transportText=env->addStaticText(L"已暂停",{810,562,1020,587},false,false,nullptr);transportText->setVisible(false);transportText->setDrawBackground(true);transportText->setBackgroundColor(irr::video::SColor(165,0,0,0));transportText->setOverrideColor(irr::video::SColor(255,255,255,255));transportText->setTextAlignment(irr::gui::EGUIA_CENTER,irr::gui::EGUIA_CENTER);
 setupWindow=env->addWindow({295,110,745,530},true,L"决构对局 · 名称与卡组",nullptr,9200);setupWindow->getCloseButton()->setVisible(false);setupWindow->setVisible(false);
 for(int p=0;p<2;++p){int y=45+p*95;
  env->addStaticText(p?L"玩家 2 名称":L"玩家 1 名称",{15,y,90,y+25},false,false,setupWindow);
  nameInputs[p]=env->addEditBox(playerNames[p].c_str(),{95,y-5,430,y+23},true,setupWindow,9781+p);nameInputs[p]->setMax(19);nameInputs[p]->setToolTipText(L"最多 19 个字符；过长名称在场面中滚动显示，留空使用默认名称。");
  env->addStaticText(p?L"玩家 2 卡组":L"玩家 1 卡组",{15,y+35,90,y+60},false,false,setupWindow);
  setupCategories[p]=env->addComboBox({95,y+30,215,y+57},setupWindow,9203+p);setupDecks[p]=env->addComboBox({225,y+30,430,y+57},setupWindow,9205+p);setupDecks[p]->setMaxSelectionRows(10);
 }
 env->addStaticText(L"玩家 1 起手",{15,230,130,256},false,false,setupWindow);startHand[0]=env->addEditBox(L"0",{130,226,195,253},true,setupWindow,9207);env->addStaticText(L"玩家 2 起手",{225,230,340,256},false,false,setupWindow);startHand[1]=env->addEditBox(L"0",{340,226,430,253},true,setupWindow,9209);
 env->addStaticText(L"名称与卡组设置在开始对局时生效。",{15,266,430,290},false,true,setupWindow);
 setupStatus=env->addStaticText(L"不检查禁限、张数、重复卡；按文件顺序载入。",{15,300,430,325},false,true,setupWindow,9208);
 env->addButton({15,335,215,363},setupWindow,9201,L"开始决构对局");env->addButton({230,335,430,363},setupWindow,9210,L"载入存档…");env->addButton({15,373,430,401},setupWindow,9202,L"取消");
 orderToggle=env->addButton({465,5,610,35},nullptr,9300,L"查看／排序牌组");orderToggle->setVisible(false);viewButton=env->addButton({312,590,557,630},nullptr,9750,L"当前玩家：1");viewButton->setVisible(false);
 orderWindow=env->addWindow({325,45,775,580},false,L"牌组顺序 · 从牌顶到牌底",nullptr,9301);orderWindow->getCloseButton()->setVisible(false);orderWindow->setVisible(false);
 orderPlayer=env->addComboBox({15,35,155,62},orderWindow,9302);orderPlayer->addItem(L"玩家 1",0);orderPlayer->addItem(L"玩家 2",1);
 orderZone=env->addComboBox({170,35,435,62},orderWindow,9303);orderZone->addItem(L"主卡组（牌顶 → 牌底）",LOCATION_DECK);orderZone->addItem(L"额外卡组",LOCATION_EXTRA);
 orderList=env->addListBox({15,75,435,385},orderWindow,9304,true);orderList->setItemHeight(22);
 env->addStaticText(L"移动到序号",{15,400,115,425},false,false,orderWindow);orderTarget=env->addEditBox(L"1",{120,395,205,423},true,orderWindow,9305);env->addButton({220,395,435,423},orderWindow,9306,L"移动到指定位置");
 const wchar_t* labels[]={L"上移",L"下移",L"置顶",L"置底"};for(int i=0;i<4;++i)env->addButton({15+i*107,435,115+i*107,463},orderWindow,9307+i,labels[i]);
 orderStatus=env->addStaticText(L"删卡保持顺序；排序／洗牌可撤回并记录录像。",{15,466,435,495},false,true,orderWindow,9312);env->addButton({15,498,177,526},orderWindow,9313,L"删除选中的卡片");env->addButton({183,498,303,526},orderWindow,9314,L"立即洗牌");env->addButton({309,498,435,526},orderWindow,9311,L"返回对局");
 saveWindow=env->addWindow({315,100,775,565},false,L"时点存档",nullptr,9500);saveWindow->getCloseButton()->setVisible(false);saveWindow->setVisible(false);
 saveFolder=env->addComboBox({15,35,445,63},saveWindow,9510);folderName=env->addEditBox(L"",{15,72,310,100},true,saveWindow,9511);folderName->setMax(60);folderName->setToolTipText(L"输入新文件夹名称，保存不同推演局面。");env->addButton({320,72,445,100},saveWindow,9512,L"新建文件夹");saveList=env->addListBox({15,110,445,230},saveWindow,9501,true);saveList->setItemHeight(24);saveLabel=env->addEditBox(L"新的时点",{15,242,445,272},true,saveWindow,9502);saveLabel->setMax(60);
 env->addButton({15,283,220,313},saveWindow,9503,L"保存当前时点");env->addButton({235,283,445,313},saveWindow,9504,L"读取选中时点");env->addButton({15,323,220,353},saveWindow,9513,L"删除选中时点");env->addButton({235,323,445,353},saveWindow,9514,L"删除当前文件夹");saveStatus=env->addStaticText(L"全局存档：每次开局或局内都可载入，可按文件夹分类。",{15,367,310,442},false,true,saveWindow,9505);env->addButton({325,407,445,440},saveWindow,9506,L"关闭");saveDeleteWindow=env->addWindow({345,210,745,410},false,L"确认删除存档",nullptr,9515);saveDeleteWindow->getCloseButton()->setVisible(false);saveDeleteWindow->setVisible(false);saveDeleteQuestion=env->addStaticText(L"",{15,35,385,125},false,true,saveDeleteWindow,9516);env->addButton({45,150,180,180},saveDeleteWindow,9517,L"确认删除");env->addButton({220,150,355,180},saveDeleteWindow,9518,L"取消");
 formWindow=env->addWindow({315,40,955,605},false,L"卡片形态、状态与限制",nullptr,9600);formWindow->getCloseButton()->setVisible(false);formWindow->setVisible(false);
 auto* tabs=env->addTabControl({15,32,625,510},formWindow,true,true,9611);auto* formTab=tabs->addTab(L"卡片形态");auto* ruleTab=tabs->addTab(L"状态与限制");auto* extraTab=tabs->addTab(L"更多状态效果");
 formContent=env->addTab({0,0,590,715},formTab,9787);formScroll=env->addScrollBar(false,{590,0,607,451},formTab,9786);formScroll->setSmallStep(30);formScroll->setLargeStep(120);
 cardForm=env->addComboBox({15,20,585,50},formContent,9601);const wchar_t* forms[]={L"原本卡片形态",L"怪兽作为永续魔法",L"怪兽作为永续陷阱",L"魔法／陷阱作为自定义怪兽",L"自身可变怪兽的陷阱（沿用原设定）"};for(int i=0;i<5;++i)cardForm->addItem(forms[i],i);cardForm->addItem(L"自定义怪兽属性／类型／卡名",5);
 env->addStaticText(L"种族",{15,72,110,99},false,false,formContent);raceBox=env->addComboBox({115,67,280,98},formContent,9602);for(uint32_t bit=1;bit && bit<=0x2000000U;bit<<=1)raceBox->addItem(dataManager.FormatRace(bit).c_str(),bit);
 env->addStaticText(L"属性",{310,72,385,99},false,false,formContent);attributeBox=env->addComboBox({390,67,585,98},formContent,9603);for(uint32_t bit=1;bit<=0x40;bit<<=1)attributeBox->addItem(dataManager.FormatAttribute(bit).c_str(),bit);
 const wchar_t* formLabels[]={L"攻击力",L"守备力",L"等级"};irr::gui::IGUIEditBox** boxes[]={&attackBox,&defenseBox,&levelBox};for(int i=0;i<3;++i){int y=118+i*43;auto* label=env->addStaticText(formLabels[i],{15,y,110,y+28},false,false,formContent);if(i==2)levelLabel=label;*boxes[i]=env->addEditBox(i==2?L"4":L"0",{115,y-3,280,y+28},true,formContent,9604+i);}
 effectMonster=env->addCheckBox(true,{310,118,585,147},formContent,9607,L"作为效果怪兽（取消则为效果以外怪兽）");keepSpellTrap=env->addCheckBox(false,{310,161,585,190},formContent,9608,L"也当作原本的魔法／陷阱卡");selfSummon=env->addCheckBox(false,{310,204,585,233},formContent,9609,L"由自身效果特殊召唤上场");
 for(int i=0;i<4;++i)stateBoxes[i]=env->addCheckBox(false,{15,280+(i==3?1:0)*35,585,307+(i==3?1:0)*35},formContent,9620+i,stateLabels[i]);stateBoxes[3]->setToolTipText(L"限制召唤、盖放、卡片／效果发动、攻击、手动变更表示，以及作为融合／同调／超量／连接素材。");
 
 for(int i=0;i<32;++i){int col=i/16,row=i%16;ruleBoxes[i]=env->addCheckBox(false,{15+col*295,10+row*25,300+col*295,34+row*25},ruleTab,9650+i,ruleLabels[i]);}
 env->addStaticText(L"勾选会施加编辑器限制；取消只移除编辑器施加的限制。",{15,413,585,451},false,true,ruleTab);
 kindLabel=env->addStaticText(L"怪兽类别",{15,430,110,458},false,false,formContent);kindBox=env->addComboBox({115,425,585,458},formContent,9735);kindBox->addItem(L"不属于这五类",0);const wchar_t* kinds[]={L"融合",L"同调",L"超量",L"连接",L"仪式"};for(int i=0;i<5;++i)kindBox->addItem(kinds[i],typeMasks[i+2]);otherTypesButton=env->addButton({15,475,585,507},formContent,9736,L"其他怪兽类型…");
 otherTypesWindow=env->addWindow({420,90,810,520},false,L"其他怪兽类型",nullptr,9738);otherTypesWindow->getCloseButton()->setVisible(false);otherTypesWindow->setVisible(false);const wchar_t* subtypeLabels[]={L"调整",L"二重",L"融合",L"同调",L"超量",L"连接",L"仪式",L"灵魂",L"同盟",L"反转",L"卡通",L"灵摆",L"特殊召唤专用"};int otherRow=0;for(int i=0;i<13;++i)if(i<2 || i>6){int y=35+otherRow++*40;typeBoxes[i]=env->addCheckBox(false,{15,y,375,y+30},otherTypesWindow,9720+i,subtypeLabels[i]);}env->addButton({115,375,275,405},otherTypesWindow,9737,L"返回卡片形态");
 arrowsLabel=env->addStaticText(L"连接箭头",{15,535,135,565},false,false,formContent,9798);const wchar_t* directions[]={L"↖ 左上",L"↑ 上",L"↗ 右上",L"← 左",L"→ 右",L"↙ 左下",L"↓ 下",L"↘ 右下"};const int cols[]={0,1,2,0,2,0,1,2},rows[]={0,0,0,1,1,2,2,2};for(int i=0;i<8;++i)arrowBoxes[i]=env->addCheckBox(false,{145+cols[i]*140,530+rows[i]*32,280+cols[i]*140,560+rows[i]*32},formContent,9790+i,directions[i]);
 nameLabel=env->addStaticText(L"卡名搜索",{15,244,110,270},false,false,formContent,9741);nameSearch=env->addEditBox(L"",{115,239,285,270},true,formContent,9742);nameBox=env->addComboBox({295,239,585,270},formContent,9740);for(const auto& row:dataManager.GetDataTable())if(row.second.type&7)nameCodes.push_back(row.first);std::sort(nameCodes.begin(),nameCodes.end(),[](uint32_t a,uint32_t b){return std::wcscmp(dataManager.GetName(a),dataManager.GetName(b))<0;});FilterNames(0);nameBox->setMaxSelectionRows(12);
 extraState=env->addComboBox({15,15,585,48},extraTab,9701);for(size_t i=0;i<sizeof(editor_catalog_codes)/sizeof(editor_catalog_codes[0]);++i)extraState->addItem(editor_catalog_labels[i],editor_catalog_codes[i]);extraState->setMaxSelectionRows(12);
 extraValue=env->addEditBox(L"1",{15,64,180,95},true,extraTab,9702);env->addButton({195,64,385,95},extraTab,9703,L"添加／更新状态");env->addButton({400,64,585,95},extraTab,9704,L"移除这个状态");
 extraList=env->addListBox({15,110,585,348},extraTab,9705,true);extraList->setItemHeight(24);env->addStaticText(L"仅对已选中的卡片生效；开关状态用添加／移除，数值状态填写数值。程序、场上规则和需要脚本回调的状态需实际效果处理。",{15,362,585,451},false,true,extraTab);
 slotTooltip=env->addStaticText(L"",{0,0,420,70},true,true,nullptr,9760,true);slotTooltip->setVisible(false);slotTooltip->setOverrideColor(irr::video::SColor(255,20,20,20));slotTooltip->setBackgroundColor(irr::video::SColor(255,255,255,225));
 InitializeAttachments();InitializeSummonHistory();
 env->addButton({15,520,625,550},formWindow,9610,L"返回编辑页面（点击生成／保存修改后生效）");FillForm(nullptr);UpdateForm();

}
void DuelEditor::Begin() {
 std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);
 available=false;create->setEnabled(false);setLP->setEnabled(false);deleteButton->setEnabled(false);viewPlayer=0;RefreshPlayerNames();promptPlayer=2;if(!timingsInitialized){timingModes[0]=(mainGame->ignore_chain?1U:0U)|(mainGame->always_chain?2U:0U)|(mainGame->chain_when_avail?4U:0U);timingModes[1]=0;timingsInitialized=true;}
 selectedID=0;currentCards.clear();existing->clear();mode->setSelected(0);FillForm(nullptr);UpdateForm();SetMode();firstEditor=true;spaceDown=false;
 for(int p=0;p<2;++p){nameInputs[p]->setText(playerNames[p].c_str());mainGame->RefreshCategoryDeck(setupCategories[p],setupDecks[p],false);}
 setupStatus->setText(L"不检查禁限、张数、重复卡；按文件顺序载入。");mainGame->wMainMenu->setVisible(false);setupWindow->setVisible(true);mainGame->env->setFocus(setupDecks[0]);
}
void DuelEditor::Available(bool enabled,uint32_t currentRevision,bool canUndo) {
 if(!active)return;
 std::lock_guard<std::mutex> lock(mainGame->gMutex);
 std::lock_guard<std::recursive_mutex> uiLock(editorGuiMutex);
 available=enabled;revision=currentRevision;
 if(enabled && firstEditor){firstEditor=false;panel->setVisible(true);}
 toggle->setVisible(true);toggle->setEnabled(true);orderToggle->setVisible(true);viewButton->setVisible(true);uint32_t timing=TimingMode(CurrentView());mainGame->ignore_chain=timing&1;mainGame->always_chain=timing&2;mainGame->chain_when_avail=timing&4;for(auto* b:{mainGame->btnChainIgnore,mainGame->btnChainAlways,mainGame->btnChainWhenAvail})b->setVisible(true);mainGame->dField.UpdateChainButtons();std::wstring viewText=L"当前玩家："+std::to_wstring(CurrentView()+1);int pending=promptPlayer;if(pending<2 && pending!=int(CurrentView()))viewText+=L" · 玩家"+std::to_wstring(pending+1)+L"待处理";viewButton->setText(viewText.c_str());
 for(int id=9306;id<=9310;++id)orderWindow->getElementFromId(id,true)->setEnabled(enabled);orderWindow->getElementFromId(9313,true)->setEnabled(enabled);orderWindow->getElementFromId(9314,true)->setEnabled(enabled);
 orderStatus->setText(enabled?L"删卡保持顺序；排序／洗牌可撤回并记录录像。":L"可以查看最新停点的牌组顺序；排序请等待阶段安全停点。");
 create->setEnabled(enabled && (mode->getSelected()==0 || (SelectedCard() && SelectedCard()->location!=LOCATION_OVERLAY)));setLP->setEnabled(enabled);
 deleteButton->setEnabled(enabled && SelectedCard());
 if(!enabled)status->setText(L"当前时点可查看卡片状态；保存修改需阶段安全停点。");
 formWindow->setText(enabled?L"卡片形态、状态与限制":L"查看卡片状态（当前时点只读）");
 toggle->setText(panel->isVisible()?L"正在编辑局面":L"进入决构编辑");
}

void DuelEditor::SummonHistories(const std::vector<std::pair<uint64_t,uint32_t>>& kinds,const std::vector<editor_summon_material_info>& materials){std::lock_guard<std::recursive_mutex> lock(editorGuiMutex);currentSummonKinds.clear();for(const auto& item:kinds)currentSummonKinds[item.first]=item.second;currentSummonMaterials=materials;}
void DuelEditor::Cards(const std::vector<editor_card_info>& cards,uint32_t currentRevision) {
 std::lock_guard<std::mutex> lock(mainGame->gMutex);
 std::lock_guard<std::recursive_mutex> uiLock(editorGuiMutex);
 currentCards=cards;if(attachmentChooseCreated){for(const auto& c:cards)if(c.code==attachmentNewCode&&c.location==LOCATION_MZONE&&c.id>selectedID)selectedID=c.id;mode->setSelected(1);attachmentChooseCreated=false;attachmentNewCode=0;}RefreshOrder();existing->clear();int selected=-1;
 for(size_t i=0;i<currentCards.size();++i) {
  const auto& card=currentCards[i];
  const wchar_t* area=card.location==LOCATION_HAND?L"手牌":card.location==LOCATION_DECK?L"卡组":card.location==LOCATION_MZONE?L"怪兽区":card.location==LOCATION_SZONE?L"魔陷区":card.location==LOCATION_GRAVE?L"墓地":card.location==LOCATION_REMOVED?L"除外":card.location==LOCATION_EXTRA?L"额外":L"超量素材";
  std::wstring text=L"玩家 "+std::to_wstring(card.player+1)+L" "+area+L" "+std::to_wstring(card.sequence+1)+L" · "+dataManager.GetName(card.effective_code?card.effective_code:card.code)+L" #"+std::to_wstring(card.id);
  if(card.owner!=card.player)text+=L"（对方拥有）";
  existing->addItem(text.c_str());if(card.id==selectedID)selected=int(i);
 }
 existing->setSelected(selected);
 if(selected<0)selectedID=0;
 if(mode->getSelected()==1 && currentRevision!=revision)FillExisting();
}
bool DuelEditor::OpenCard(uint32_t playerID,uint32_t zone,uint32_t seq) {
 std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);
 if(!active)return false;
 for(size_t i=0;i<currentCards.size();++i)if(currentCards[i].player==playerID && currentCards[i].location==zone && currentCards[i].sequence==seq) {
  selectedID=currentCards[i].id;mode->setSelected(1);existing->setSelected(int(i));SetMode();
  mainGame->ShowCardInfo(currentCards[i].code);panel->setVisible(true);panel->getParent()->bringToFront(panel);mainGame->env->setFocus(existing);return true;
 }
 return false;
}
void DuelEditor::Attachments(const std::vector<editor_material_info>& materials,const std::vector<editor_counter_info>& counters){std::lock_guard<std::mutex> lock(mainGame->gMutex);std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);currentMaterials=materials;currentCounters=counters;}
void DuelEditor::CustomEffects(const std::vector<editor_effect_info>& effects){std::lock_guard<std::mutex> lock(mainGame->gMutex);std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);customEffects=effects;RefreshCustom();}
void DuelEditor::Completed(int result) {
 if(result==EDITOR_OK&&attachmentNewCode)attachmentChooseCreated=true;
 if(!active)return;
 std::lock_guard<std::mutex> lock(mainGame->gMutex);
 std::lock_guard<std::recursive_mutex> uiLock(editorGuiMutex);
 const wchar_t* texts[]={L"修改已应用；退出编辑后可正常操作双方",L"当前不是决构测试对局",L"请等待当前召唤或连锁处理结束后的阶段安全停点",L"局面已变化，请重新提交",L"参数无效",L"卡片数据库中没有这张卡",L"卡片种类、位置或表示形式不适合目标区域",L"目标区域已占用或被禁用"};
 status->setText(result>=0 && result<8?texts[result]:L"录像空间不足；本次修改未应用");
}
void DuelEditor::Shutdown() {
 std::lock_guard<std::mutex> lock(mainGame->gMutex);
 std::lock_guard<std::recursive_mutex> uiLock(editorGuiMutex);
 active=false;available=false;if(summonHistoryWindow)summonHistoryWindow->setVisible(false);if(attachmentWindow)attachmentWindow->setVisible(false);attachmentReady=false;saveDeleteWindow->setVisible(false);deleteSavePath.clear();otherTypesWindow->setVisible(false);drawWindow->setVisible(false);panel->setVisible(false);toggle->setVisible(false);orderWindow->setVisible(false);orderToggle->setVisible(false);viewButton->setVisible(false);promptPlayer=2;undo->setVisible(false);pauseButton->setVisible(false);stepButton->setVisible(false);saveButton->setVisible(false);transportText->setVisible(false);saveWindow->setVisible(false);formWindow->setVisible(false);
}
void DuelEditor::Transport(bool isPaused,bool canBack,bool canForward,bool boundary,size_t point){
 if(!active)return;std::lock_guard<std::mutex> lock(mainGame->gMutex);std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);
 for(auto* b:{undo,pauseButton,stepButton,saveButton})b->setVisible(true);transportText->setVisible(true);undo->setEnabled(canBack);stepButton->setEnabled(isPaused);saveButton->setEnabled(true);
 std::wstring text=(isPaused?L"已暂停 · 时点 ":L"运行中 · 时点 ")+std::to_wstring(point);if(canForward)text+=L"（可前进）";transportText->setText(text.c_str());pauseButton->setToolTipText(isPaused?L"继续对局（空格）":L"暂停（空格）");
}
void DuelEditor::SaveResult(const wchar_t* text){std::lock_guard<std::mutex> lock(mainGame->gMutex);std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);RefreshSaves();saveStatus->setText(text);}
void DuelEditor::Layout(){
 if(!setupWindow)return;std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);static uint32_t width=0,height=0;
 if(width==mainGame->window_size.Width && height==mainGame->window_size.Height)return;width=mainGame->window_size.Width;height=mainGame->window_size.Height;
 int cx=width/2,cy=height/2;setupWindow->setRelativePosition({cx-225,cy-210,cx+225,cy+210});int editCy=(height-60)/2;panel->setRelativePosition({cx-205,std::max(5,editCy-290),cx+205,std::max(5,editCy-290)+580});formWindow->setRelativePosition({cx-320,cy-282,cx+320,cy+283});saveWindow->setRelativePosition({cx-230,cy-232,cx+230,cy+233});orderWindow->setRelativePosition({cx-225,cy-267,cx+225,cy+268});
 int right=width-8,bottom=height-8;int left=mainGame->wInfos->getAbsolutePosition().LowerRightCorner.X+8;int viewLeft=left+282;int viewWidth=std::max(140,std::min(245,right-211-viewLeft-14));toggle->setRelativePosition({left,bottom-40,left+135,bottom});orderToggle->setRelativePosition({left+141,bottom-40,left+276,bottom});viewButton->setRelativePosition({viewLeft,bottom-40,viewLeft+viewWidth,bottom});saveButton->setRelativePosition({right-64,bottom-40,right,bottom});stepButton->setRelativePosition({right-113,bottom-40,right-71,bottom});pauseButton->setRelativePosition({right-162,bottom-40,right-120,bottom});undo->setRelativePosition({right-211,bottom-40,right-169,bottom});transportText->setRelativePosition({right-280,bottom-68,right,bottom-43});
}
void DuelEditor::DrawTransport(){
 std::lock_guard<std::recursive_mutex> uiLock(editorGuiMutex);
 if(active && panel->isVisible()){int index=results->getSelected();uint32_t code=index>=0 && index<int(codes.size())?codes[index]:0;uint32_t zone=location->getItemData(location->getSelected());int form=Shape();if(code!=summonChoiceCode || zone!=summonChoiceZone || form!=summonChoiceForm || position->getItemData(position->getSelected())!=summonChoicePosition || EffectiveType()!=summonChoiceType)UpdateSummonChoices();}

 if(!active || !panel->isVisible() || !slot->isVisible())slotTooltip->setVisible(false);
 if(!active)return;std::lock_guard<std::recursive_mutex> editorLock(editorGuiMutex);
 for(int i=0;i<2;++i){auto* button=i?stepButton:undo;if(!button->isVisible())continue;auto rect=button->getAbsolutePosition();int cx=(rect.UpperLeftCorner.X+rect.LowerRightCorner.X)/2,cy=(rect.UpperLeftCorner.Y+rect.LowerRightCorner.Y)/2;auto color=irr::video::SColor(255,button->isEnabled()?35:160,button->isEnabled()?35:160,button->isEnabled()?35:160);for(int x=0;x<=14;++x){int half=9*(14-x)/14;int px=i?cx-7+x:cx+7-x;mainGame->driver->draw2DLine({px,cy-half},{px,cy+half},color);}}
 if(pauseButton->isVisible()){auto r=pauseButton->getAbsolutePosition();int cx=(r.UpperLeftCorner.X+r.LowerRightCorner.X)/2,cy=(r.UpperLeftCorner.Y+r.LowerRightCorner.Y)/2;auto c=irr::video::SColor(255,35,35,35);mainGame->driver->draw2DRectangle(c,{cx-6,cy-9,cx-2,cy+9});mainGame->driver->draw2DRectangle(c,{cx+2,cy-9,cx+6,cy+9});}
}
bool DuelEditor::OnEvent(const irr::SEvent& event) {
 if(!setupWindow)return false;
 if(mainGame->wReplaySave->isVisible() && mainGame->dInfo.isSingleMode && !active && event.EventType==irr::EET_GUI_EVENT && event.GUIEvent.EventType==irr::gui::EGET_BUTTON_CLICKED){int id=event.GUIEvent.Caller->getID();if(id==BUTTON_REPLAY_SAVE || id==BUTTON_REPLAY_CANCEL){if(id==BUTTON_REPLAY_SAVE && !mainGame->ebRSName->getText()[0])return true;mainGame->actionParam=id==BUTTON_REPLAY_SAVE;mainGame->HideElement(mainGame->wReplaySave);mainGame->replaySignal.Set();return true;}}
 std::lock_guard<std::recursive_mutex> uiLock(editorGuiMutex);
 // Raise all ancestors of an open combo popup, so buttons in outer panels cannot cover it.
 if(event.EventType==irr::EET_GUI_EVENT && event.GUIEvent.EventType==irr::gui::EGET_ELEMENT_FOCUSED){auto* e=event.GUIEvent.Caller;if(e->getType()==irr::gui::EGUIET_LIST_BOX && e->getParent() && e->getParent()->getType()==irr::gui::EGUIET_COMBO_BOX)for(;e->getParent();e=e->getParent())e->getParent()->bringToFront(e);}
 // A completed selection belongs to the page; wheel input must not change a closed combo.
 if(event.EventType==irr::EET_GUI_EVENT && event.GUIEvent.EventType==irr::gui::EGET_COMBO_BOX_CHANGED){auto* caller=event.GUIEvent.Caller;if(caller->getID()>=9000 && caller->getID()<9800)mainGame->env->setFocus(caller->getParent());}
 if(event.EventType==irr::EET_MOUSE_INPUT_EVENT && event.MouseInput.Event==irr::EMIE_MOUSE_WHEEL){
  irr::core::position2di point(event.MouseInput.X,event.MouseInput.Y);
  auto* hit=mainGame->env->getRootGUIElement()->getElementFromPoint(point);
  auto* focus=mainGame->env->getFocus();bool popup=false,inForm=false,closedCombo=false;
  for(auto* e=focus;e;e=e->getParent())if(e->getType()==irr::gui::EGUIET_LIST_BOX && e->getParent() && e->getParent()->getType()==irr::gui::EGUIET_COMBO_BOX)popup=true;
  for(auto* e=hit;e;e=e->getParent()){
   if(e==formWindow)inForm=true;
   if(e->getType()==irr::gui::EGUIET_COMBO_BOX && e->getID()>=9000 && e->getID()<9800)closedCombo=true;
  }
  if(!popup && inForm && formWindow->isVisible()){
   auto* tabs=static_cast<irr::gui::IGUITabControl*>(formWindow->getElementFromId(9611,true));
   if(tabs->getActiveTab()==0 && formContent->getParent()->getAbsolutePosition().isPointInside(point) && formScroll->getMax()>0){
    formScroll->setPos(formScroll->getPos()-int(event.MouseInput.Wheel*formScroll->getSmallStep()));
    int scroll=formScroll->getPos();formContent->setRelativePosition({0,-scroll,590,715-scroll});return true;
   }
  }
  if(!popup && closedCombo)return true;
 }
 // Focus and hover events must reach Irrlicht. Consuming FOCUS_LOST traps all real clicks in a hidden prompt.
 if(event.EventType==irr::EET_GUI_EVENT){int id=event.GUIEvent.Caller->getID();auto type=event.GUIEvent.EventType;if(id>=9900&&id<=9918){if(id==9901&&type==irr::gui::EGET_CHECKBOX_CHANGED){UpdateSummonHistoryUI();return true;}if(id==9902&&type==irr::gui::EGET_BUTTON_CLICKED){OpenSummonHistory();return true;}if((id==9903&&type==irr::gui::EGET_COMBO_BOX_CHANGED)||(id==9904&&type==irr::gui::EGET_EDITBOX_CHANGED)){FilterSummonMaterials();return true;}if((id==9909||id==9910||id==9907)&&type==irr::gui::EGET_COMBO_BOX_CHANGED){UpdateSummonMaterialPositionControls();return true;}if(id==9905&&type==irr::gui::EGET_LISTBOX_CHANGED){SelectSummonMaterial();return true;}if(id==9915&&type==irr::gui::EGET_BUTTON_CLICKED){AddSummonMaterial();return true;}if(id==9917&&type==irr::gui::EGET_BUTTON_CLICKED){int row=summonMaterialRows->getSelected();if(row>=0&&size_t(row)<summonHistoryDraft.size()){summonHistoryDraft.erase(summonHistoryDraft.begin()+row);RefreshSummonMaterialRows();}return true;}if(id==9918&&type==irr::gui::EGET_BUTTON_CLICKED){summonHistoryWindow->setVisible(false);panel->setVisible(true);return true;}}}
 if(event.EventType==irr::EET_GUI_EVENT){auto kind=event.GUIEvent.EventType;if(kind==irr::gui::EGET_ELEMENT_FOCUS_LOST || kind==irr::gui::EGET_ELEMENT_FOCUSED || kind==irr::gui::EGET_ELEMENT_HOVERED || kind==irr::gui::EGET_ELEMENT_LEFT)return false;}
 if(event.EventType==irr::EET_GUI_EVENT){int id=event.GUIEvent.Caller->getID();auto type=event.GUIEvent.EventType;if(active&&(id==9870||id==9871)&&type==irr::gui::EGET_BUTTON_CLICKED){OpenAttachments(id==9871);return true;}if(type==irr::gui::EGET_ELEMENT_CLOSED&&id==9872){attachmentWindow->setVisible(false);formWindow->setVisible(true);return true;}if(type==irr::gui::EGET_BUTTON_CLICKED&&(id==9873||id==9874)){attachmentWindow->setVisible(false);formWindow->setVisible(true);mainGame->env->setFocus(nullptr);return true;}if(type==irr::gui::EGET_EDITBOX_CHANGED&&id==9880){FilterAttachments();return true;}if(type==irr::gui::EGET_BUTTON_CLICKED&&(id==9886||id==9887||id==9888)){AttachmentAction(id);return true;}if(type==irr::gui::EGET_LISTBOX_CHANGED&&id==9883&&attachmentMaterial){int row=attachmentResults->getSelected();if(row>=0&&size_t(row)<attachmentMatches.size())mainGame->ShowCardInfo(attachmentMatches[row]);return true;}if(type==irr::gui::EGET_LISTBOX_CHANGED&&id==9884&&!attachmentMaterial){int row=attachmentRows->getSelected();if(row>=0&&size_t(row)<counterRows.size())attachmentAmount->setText(std::to_wstring(draftCounters[counterRows[row]]).c_str());return true;}}
 if(event.EventType==irr::EET_GUI_EVENT) {
  int id=event.GUIEvent.Caller->getID();auto type=event.GUIEvent.EventType;
  if(saveDeleteWindow->isVisible()){
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9518){saveDeleteWindow->setVisible(false);deleteSavePath.clear();return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9517){std::wstring verified;bool ok=SafeSaveDeletePath(deleteSavePath,verified);if(ok && deleteSaveFolder){size_t points=0,files=0;ok=CountSaveFolder(verified,points,files) && FileSystem::DeleteDir(verified.c_str()) && !FileSystem::IsDirExists(verified.c_str());}else if(ok)ok=FileSystem::RemoveFile(verified.c_str());saveDeleteWindow->setVisible(false);deleteSavePath.clear();RefreshSaves();saveStatus->setText(ok?(deleteSaveFolder?L"文件夹及其中内容已删除":L"选中时点已删除"):L"删除失败，未删除的内容已保留");return true;}
   return true;
  }
  if(setupWindow->isVisible()) {
   if(type==irr::gui::EGET_COMBO_BOX_CHANGED && (id==9203 || id==9204)){int p=id-9203;mainGame->RefreshDeck(setupCategories[p],setupDecks[p]);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9210){startupSaveSelection=true;RefreshSaves();setupWindow->setVisible(false);static_cast<irr::gui::IGUIButton*>(saveWindow->getElementFromId(9503,true))->setVisible(false);saveLabel->setVisible(false);saveStatus->setText(L"选择全局存档，载入后暂停在保存的时点。");saveWindow->setVisible(true);mainGame->env->setFocus(saveFolder);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9201){StartConfiguredDuel();return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9202){setupWindow->setVisible(false);mainGame->wMainMenu->setVisible(true);mainGame->env->setFocus(mainGame->wMainMenu);return true;}
  }
  if(saveWindow->isVisible()){
   if(type==irr::gui::EGET_BUTTON_CLICKED && (id==9513 || id==9514)){deleteSaveFolder=id==9514;std::wstring path,message;if(deleteSaveFolder){if(saveFolder->getSelected()<=0){saveStatus->setText(L"不能删除全局存档根文件夹");return true;}path=CurrentSaveFolder();size_t points=0,files=0;if(!CountSaveFolder(path,points,files)){saveStatus->setText(L"文件夹包含链接或无法检查的路径，不能删除");return true;}message=L"删除文件夹「"+saveFolders[saveFolder->getSelected()]+L"」及其中全部内容？\n共 "+std::to_wstring(points)+L" 个时点，"+std::to_wstring(files)+L" 个文件。";}else{int i=saveList->getSelected();if(i<0 || size_t(i)>=saveFiles.size()){saveStatus->setText(L"请先选择要删除的时点");return true;}wchar_t decoded[2048]{};BufferIO::DecodeUTF8(saveFiles[i].c_str(),decoded);path=decoded;message=L"删除选中的时点？\n"+std::wstring(saveList->getListItem(i));}if(!SafeSaveDeletePath(path,deleteSavePath)){saveStatus->setText(L"无法删除该路径，存档保持不变");return true;}saveDeleteQuestion->setText(message.c_str());saveDeleteWindow->setVisible(true);saveDeleteWindow->getParent()->bringToFront(saveDeleteWindow);mainGame->env->setFocus(saveDeleteWindow);return true;}
   if(type==irr::gui::EGET_COMBO_BOX_CHANGED && id==9510){RefreshSaves();return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9512){std::wstring name=folderName->getText();wchar_t safe[128]{};BufferIO::CopyWideString(name.c_str(),safe);FileSystem::SafeFileName(safe);if(name.empty() || name==L"." || name==L".." || name!=safe){saveStatus->setText(L"请输入有效的文件夹名称，不能包含路径分隔符");return true;}if(!FileSystem::MakeDir((saveRoot+L"/"+name).c_str()) && !FileSystem::IsDirExists((saveRoot+L"/"+name).c_str())){saveStatus->setText(L"无法建立文件夹");return true;}RefreshSaveFolders();auto i=std::find(saveFolders.begin(),saveFolders.end(),name);saveFolder->setSelected(int(i-saveFolders.begin()));RefreshSaves();saveStatus->setText(L"已建立并切换到新文件夹");return true;}
   if(startupSaveSelection && type==irr::gui::EGET_BUTTON_CLICKED && id==9506){startupSaveSelection=false;saveWindow->setVisible(false);setupWindow->setVisible(true);return true;}
   if(startupSaveSelection && type==irr::gui::EGET_BUTTON_CLICKED && id==9504){int i=saveList->getSelected();if(i>=0 && size_t(i)<saveFiles.size())StartFromSave(saveFiles[i]);else saveStatus->setText(L"请先选择要载入的存档");return true;}
  }
  if(active) {
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==BUTTON_LEAVE_GAME){mainGame->singleSignal.SetNoWait(true);SingleMode::StopPlay(false);return true;}
   if(type==irr::gui::EGET_EDITBOX_CHANGED && id==9742){FilterNames(nameBox->getItemData(nameBox->getSelected()));return true;}
   if(type==irr::gui::EGET_SCROLL_BAR_CHANGED && id==9786){int scroll=formScroll->getPos();formContent->setRelativePosition({0,-scroll,590,715-scroll});return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9520){drawPlayer=player->getItemData(player->getSelected());drawWindow->setText((L"玩家 "+std::to_wstring(drawPlayer+1)+L" · 立即抽卡").c_str());drawStatus->setText(L"请输入 1～255 张，将按当前牌组顺序抽卡。");mainGame->PopupElement(drawWindow);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9523){mainGame->HideElement(drawWindow);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9522){uint32_t count=0;if(!Number(drawCount,count) || !count || count>255){drawStatus->setText(L"抽卡数量必须是 1～255 的整数");return true;}if(!available){drawStatus->setText(L"请先完成当前待处理操作，再执行抽卡");return true;}editor_command command{};command.opcode=8;command.player=drawPlayer;command.revision=revision;command.value=count;if(SingleMode::QueueEditorCommand(command)){mainGame->HideElement(drawWindow);status->setText(L"正在按指定数量抽卡…");}return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9736){mainGame->PopupElement(otherTypesWindow);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9737){mainGame->HideElement(otherTypesWindow);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9300){panel->setVisible(false);RefreshOrder();orderWindow->setVisible(true);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9314){ShuffleOrderedDeck();return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9313){DeleteOrderedCard();return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id==9311){orderWindow->setVisible(false);mainGame->env->setFocus(nullptr);return true;}
   if(type==irr::gui::EGET_BUTTON_CLICKED && id>=9306 && id<=9310){SortDeck(id);return true;}
   if(type==irr::gui::EGET_COMBO_BOX_CHANGED && (id==9302 || id==9303)){orderList->setSelected(-1);RefreshOrder();return true;}
   if(type==irr::gui::EGET_LISTBOX_CHANGED && id==9304){int i=orderList->getSelected();if(i>=0 && i<int(orderedCards.size()))mainGame->ShowCardInfo(orderedCards[i].code);return true;}
  }
 }
 if(!active)return false;
 if(event.EventType==irr::EET_MOUSE_INPUT_EVENT && event.MouseInput.Event==irr::EMIE_MOUSE_MOVED){bool hover=panel->isVisible() && slot->isVisible() && slot->getAbsolutePosition().isPointInside({event.MouseInput.X,event.MouseInput.Y});slotTooltip->setVisible(hover);if(hover){slotTooltip->setText(slotExplanation.c_str());auto dim=mainGame->GetGUIFontDimension(slotExplanation.c_str());int w=int(dim.Width)+20,h=int(dim.Height)+16,x=std::min(event.MouseInput.X+10,int(mainGame->window_size.Width)-w-8),y=std::min(event.MouseInput.Y+24,int(mainGame->window_size.Height)-h-8);slotTooltip->setRelativePosition({x,y,x+w,y+h});slotTooltip->getParent()->bringToFront(slotTooltip);}}
 if(event.EventType==irr::EET_KEY_INPUT_EVENT && event.KeyInput.Key==irr::KEY_SPACE){if(mainGame->env->getFocus()&&mainGame->env->getFocus()->getType()==irr::gui::EGUIET_EDIT_BOX)return false;if(event.KeyInput.PressedDown && !spaceDown){spaceDown=true;SingleMode::TogglePause();if(!SingleMode::paused){panel->setVisible(false);formWindow->setVisible(false);if(attachmentWindow)attachmentWindow->setVisible(false);orderWindow->setVisible(false);saveWindow->setVisible(false);}}if(!event.KeyInput.PressedDown)spaceDown=false;return true;}
 if(event.EventType==irr::EET_GUI_EVENT) {
  auto id=event.GUIEvent.Caller->getID();
  if(event.GUIEvent.EventType==irr::gui::EGET_CHECKBOX_CHANGED && (id==9772 || id==9773 || id==9774)){if(location->getItemData(location->getSelected())==LOCATION_MZONE && !specialOrigin->isChecked() && !normalOrigin->isChecked() && !flipOrigin->isChecked())static_cast<irr::gui::IGUICheckBox*>(event.GUIEvent.Caller)->setChecked(true);if((id==9772 && !specialOrigin->isChecked()) || (id==9773 && normalOrigin->isChecked()) || (id==9774 && flipOrigin->isChecked()))selfSummon->setChecked(false);if(id==9772 && specialOrigin->isChecked()){normalOrigin->setChecked(false);flipOrigin->setChecked(false);}if(id==9773 && normalOrigin->isChecked()){specialOrigin->setChecked(false);flipOrigin->setChecked(false);}if(id==9774 && flipOrigin->isChecked()){specialOrigin->setChecked(false);normalOrigin->setChecked(false);}UpdateSummonChoices();return true;}
  if(event.GUIEvent.EventType==irr::gui::EGET_CHECKBOX_CHANGED && id>=9790 && id<=9797){UpdateForm();UpdateSummonChoices();return true;}
  if(event.GUIEvent.EventType==irr::gui::EGET_CHECKBOX_CHANGED && ((id>=9720 && id<=9732) || id==9607 || id==9608 || id==9609)){UpdateForm();UpdateSummonChoices();return true;}
  if(event.GUIEvent.EventType==irr::gui::EGET_COMBO_BOX_CHANGED && (id==9601 || id==9019 || id==9771 || id==9735)){UpdateForm();UpdateSummonChoices();return true;}
  if(event.GUIEvent.EventType==irr::gui::EGET_BUTTON_CLICKED){
   if(id==BUTTON_CHAIN_IGNORE || id==BUTTON_CHAIN_ALWAYS || id==BUTTON_CHAIN_WHENAVAIL){mainGame->ignore_chain=id==BUTTON_CHAIN_IGNORE && mainGame->btnChainIgnore->isPressed();mainGame->always_chain=id==BUTTON_CHAIN_ALWAYS && mainGame->btnChainAlways->isPressed();mainGame->chain_when_avail=id==BUTTON_CHAIN_WHENAVAIL && mainGame->btnChainWhenAvail->isPressed();mainGame->dField.UpdateChainButtons();return true;}
   if(id==9750){SingleMode::QueueViewSwitch();return true;}
   if(id==9401){SingleMode::TogglePause();if(!SingleMode::paused){panel->setVisible(false);formWindow->setVisible(false);if(attachmentWindow)attachmentWindow->setVisible(false);orderWindow->setVisible(false);saveWindow->setVisible(false);}return true;}
   if(id==9402){panel->setVisible(false);formWindow->setVisible(false);if(attachmentWindow)attachmentWindow->setVisible(false);SingleMode::Step();return true;}
   if(id==9403){startupSaveSelection=false;static_cast<irr::gui::IGUIButton*>(saveWindow->getElementFromId(9503,true))->setVisible(true);saveLabel->setVisible(true);if(!SingleMode::paused)SingleMode::TogglePause();panel->setVisible(false);orderWindow->setVisible(false);RefreshSaves();saveWindow->setVisible(true);return true;}
   if(id==9506){saveWindow->setVisible(false);return true;}
   if(id==9503){char name[256]{};BufferIO::EncodeUTF8(saveLabel->getText(),name);auto stamp=std::chrono::system_clock::now().time_since_epoch();auto id=std::chrono::duration_cast<std::chrono::microseconds>(stamp).count();SingleMode::QueueSave(Utf8Path(CurrentSaveFolder())+"/"+std::to_string(id)+".yges",name);return true;}
   if(id==9504){int i=saveList->getSelected();if(i>=0 && i<int(saveFiles.size())){SingleMode::QueueLoad(saveFiles[i]);saveWindow->setVisible(false);}return true;}
   if(id==9703 || id==9704){SubmitExtra(id==9704);return true;}
   if(id==9012){formWindow->setVisible(true);panel->setVisible(false);return true;}
   if(id==9610){formWindow->setVisible(false);panel->setVisible(true);return true;}
  }
  if(event.GUIEvent.EventType==irr::gui::EGET_COMBO_BOX_CHANGED && id==9011 && mode->getSelected()==1) {
   uint32_t selectedOwner=ownerOther->isChecked()?1-formPlayer:formPlayer;
   formPlayer=uint32_t(player->getSelected());ownerOther->setChecked(selectedOwner!=formPlayer);return true;
  }
  if(event.GUIEvent.EventType==irr::gui::EGET_COMBO_BOX_CHANGED && id==MODE){SetMode();return true;}
  if(event.GUIEvent.EventType==irr::gui::EGET_LISTBOX_CHANGED && id==EXISTING) {
   int index=existing->getSelected();if(index>=0 && index<int(currentCards.size())){selectedID=currentCards[index].id;FillExisting();mainGame->ShowCardInfo(currentCards[index].code);}return true;
  }
  if(event.GUIEvent.EventType==irr::gui::EGET_COMBO_BOX_CHANGED && id==REGION) {
   auto loc=location->getItemData(location->getSelected());
   if(loc==LOCATION_EXTRA || loc==LOCATION_DECK || loc==LOCATION_HAND)position->setSelected(2);
   else position->setSelected(0);
   UpdateSummonChoices();
   if(loc!=LOCATION_MZONE)summonTurn->setSelected(0);
   slot->setText(L"1");return true;
  }
  if((event.GUIEvent.EventType==irr::gui::EGET_LISTBOX_CHANGED || event.GUIEvent.EventType==irr::gui::EGET_LISTBOX_SELECTED_AGAIN) && id==RESULTS) {
   int index=results->getSelected();
   if(index>=0 && index<int(codes.size()))mainGame->ShowCardInfo(codes[index]);
   UpdateForm();UpdateSummonChoices();
   return true;
  }
  if(event.GUIEvent.EventType==irr::gui::EGET_BUTTON_CLICKED) {
   switch(id) {
   case TOGGLE:{if(!SingleMode::paused)SingleMode::TogglePause();if(mode->getSelected()==1)FillExisting();panel->setVisible(true);panel->getParent()->bringToFront(panel);mainGame->env->setFocus(search);}return true;
   case SEARCH:Search();return true;
   case CREATE:Submit(false);return true;
   case DELETE_CARD:Submit(false,true);return true;
   case LP:Submit(true);return true;
   case UNDO:
    if(SingleMode::QueueUndo()) {
     available=false;create->setEnabled(false);setLP->setEnabled(false);undo->setEnabled(false);
     status->setText(L"正在恢复上一步的局面、效果状态和次数限制…");
    }
    return true;
   case CONTINUE:if(SingleMode::paused)SingleMode::TogglePause();panel->setVisible(false);mainGame->env->setFocus(nullptr);toggle->setText(L"进入决构编辑");return true;
   }
  }
 }
 if(!panel->isVisible() && event.EventType==irr::EET_MOUSE_INPUT_EVENT && event.MouseInput.Event==irr::EMIE_RMOUSE_LEFT_UP && event.MouseInput.Control) {
  auto pos=mainGame->ResizeReverse(event.MouseInput.X,event.MouseInput.Y);auto& field=mainGame->dField;field.GetHoverField(pos.X,pos.Y);
  if(OpenCard(mainGame->LocalPlayer(field.hovered_controler),field.hovered_location,uint32_t(field.hovered_sequence)))return true;
 }
 if(promptPlayer<2 && promptPlayer!=int(CurrentView())){
  if(event.EventType==irr::EET_GUI_EVENT){int id=event.GUIEvent.Caller->getID();bool information=false;for(auto* e=event.GUIEvent.Caller;e;e=e->getParent())if(e==mainGame->wInfos)information=true;if(!information && id!=BUTTON_LEAVE_GAME && id!=BUTTON_CHAIN_IGNORE && id!=BUTTON_CHAIN_ALWAYS && id!=BUTTON_CHAIN_WHENAVAIL && id!=BUTTON_CARD_DISP_OK)return true;}
  // Hover and the information pane remain usable; only duel decisions are withheld.
  if(event.EventType==irr::EET_MOUSE_INPUT_EVENT){if(event.MouseInput.Event==irr::EMIE_MOUSE_MOVED || event.MouseInput.Event==irr::EMIE_MOUSE_WHEEL)return false;mainGame->env->postEventFromUser(event);return true;}
  if(event.EventType==irr::EET_KEY_INPUT_EVENT){if(event.KeyInput.Key==irr::KEY_KEY_A || event.KeyInput.Key==irr::KEY_KEY_S || event.KeyInput.Key==irr::KEY_KEY_D)return false;mainGame->env->postEventFromUser(event);return true;}
 }
 // Preserve pending duel choices while an editor window is in front.
 if(!available && promptPlayer<2 && (panel->isVisible() || orderWindow->isVisible() || formWindow->isVisible() || saveWindow->isVisible()) && event.EventType==irr::EET_GUI_EVENT){int id=event.GUIEvent.Caller->getID();if((id<9000 || id>9750) && id!=BUTTON_LEAVE_GAME && id!=BUTTON_CHAIN_IGNORE && id!=BUTTON_CHAIN_ALWAYS && id!=BUTTON_CHAIN_WHENAVAIL)return true;}
 // The modal editor consumes board mouse/key input; leaving a test session is still allowed.
 if((panel->isVisible() || orderWindow->isVisible() || formWindow->isVisible() || saveWindow->isVisible()) && (event.EventType==irr::EET_MOUSE_INPUT_EVENT || event.EventType==irr::EET_KEY_INPUT_EVENT)) {
  mainGame->env->postEventFromUser(event);
  return true;
 }
 return false;
}
}
