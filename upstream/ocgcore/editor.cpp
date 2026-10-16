#include "editor.h"
#include "duel.h"
#include "field.h"
#include "card.h"
#include "interpreter.h"
#include <iterator>
#include <limits>
#include <algorithm>
#include <vector>
#include "effect.h"
#include <sstream>
#include "editor_rules.h"
#include "editor_catalog.h"
#include <string>
#include <regex>
#include <cstring>

static thread_local duel* editor_group=nullptr;
void editor_enable(intptr_t handle, int enabled) {
 if(handle){auto* d=reinterpret_cast<duel*>(handle);d->editor_enabled=enabled!=0;d->editor_boundaries=enabled>1;d->editor_opening=enabled>2;}
}
uint32_t editor_revision(intptr_t handle) {
 return handle ? reinterpret_cast<duel*>(handle)->editor_revision : 0;
}
int editor_can_edit(intptr_t handle) {
 if(!handle) return 0;
 auto* d=reinterpret_cast<duel*>(handle);
 if(editor_group==d)return 1;
 auto* f=d->game_field;
 const auto& c=f->core;
 if(!d->editor_enabled || d->buffer_size() || d->lua->call_depth || !d->lua->coroutines.empty()
    || !c.current_chain.empty() || !c.new_chains.empty() || c.chain_solving || c.conti_solving
    || c.summoning_card || !c.solving_continuous.empty() || !c.sub_solving_continuous.empty())return 0;
 if(d->editor_boundaries){
  // A turn boundary carries no stale card selection. Pending adjustment is safe to replace.
  bool turn=false,plain=true;
  for(const auto* queue:{&c.units,&c.subunits})for(const auto& u:*queue){
   turn|=u.type==PROCESSOR_TURN;plain&=u.type==PROCESSOR_TURN || (u.type==PROCESSOR_ADJUST && u.step==0) || (d->editor_opening && f->infos.turn_id==0 && u.type==PROCESSOR_DRAW && (u.step==1 || (u.step==0 && ((u.arg2>>4)&0xf)==PLAYER_NONE)));
  }
  if(turn && plain && c.new_fchain.empty() && c.new_ochain.empty())return 1;
 }
 if(!d->editor_waiting || !c.subunits.empty() || c.units.size()<2)return 0;
 const auto& select=c.units.front();const auto& parent=*std::next(c.units.begin());
 if(select.step!=1 || parent.step!=1)return 0;
 if(select.type==PROCESSOR_SELECT_IDLECMD && parent.type==PROCESSOR_IDLE_COMMAND)return 1;
 return d->editor_boundaries && ((select.type==PROCESSOR_SELECT_BATTLECMD && parent.type==PROCESSOR_BATTLE_COMMAND)
    || (select.type==PROCESSOR_SELECT_CHAIN && parent.type==PROCESSOR_PHASE_EVENT));
}

namespace {
uint32_t CardFlags(const card* c) {
 uint32_t flags=c->is_status(STATUS_PROC_COMPLETE)?EDITOR_PROC_COMPLETE:0;
 if(c->is_status(STATUS_SUMMON_TURN))flags|=EDITOR_SUMMON_NORMAL;
 else if(c->is_status(STATUS_SPSUMMON_TURN))flags|=EDITOR_SUMMON_SPECIAL;
 else if(c->is_status(STATUS_FLIP_SUMMON_TURN))flags|=EDITOR_SUMMON_FLIP;
 if(c->current.location==LOCATION_GRAVE && c->turnid==c->pduel->game_field->infos.turn_id)flags|=EDITOR_SENT_GRAVE_TURN;
 return flags;
}
void ControlOverride(duel* d,card* c,uint8_t player) {
 std::vector<effect*> removed;
 for(const auto& entry:c->single_effect)if(entry.second->editor_control)removed.push_back(entry.second);
 for(auto* e:removed)c->remove_effect(e);
 if(!(c->current.location&LOCATION_ONFIELD))return;
 if(c->owner==player && std::get<uint8_t>(c->refresh_control_status())==player)return;
 auto* e=d->new_effect();e->editor_control=true;e->owner=c;e->handler=c;
 e->type=EFFECT_TYPE_SINGLE;e->code=EFFECT_SET_CONTROL;e->value=player;
 e->flag[0]=EFFECT_FLAG_CANNOT_DISABLE|EFFECT_FLAG_UNCOPYABLE|EFFECT_FLAG_IGNORE_IMMUNE;
 e->reset_flag=RESET_EVENT|RESET_LEAVE|RESET_TOHAND|RESET_TODECK|RESET_TOGRAVE|RESET_REMOVE|RESET_OVERLAY;
 c->add_effect(e);
}
void TurnState(field* f,card* c,uint32_t flags) {
 const uint32_t summon=flags&14U;
 c->set_status(STATUS_PROC_COMPLETE,!!(flags&EDITOR_PROC_COMPLETE));
 c->set_status(STATUS_SUMMON_TURN|STATUS_SPSUMMON_TURN|STATUS_FLIP_SUMMON_TURN,FALSE);
 if(c->current.location==LOCATION_MZONE) {
  c->set_status(STATUS_CANNOT_CHANGE_FORM,!!summon);
  c->turnid=summon?f->infos.turn_id:(f->infos.turn_id?f->infos.turn_id-1:0);
  c->summon_info=0;c->summon_player=PLAYER_NONE;
  if(summon) {
   c->set_status(summon==EDITOR_SUMMON_NORMAL?STATUS_SUMMON_TURN:(summon==EDITOR_SUMMON_SPECIAL?STATUS_SPSUMMON_TURN:STATUS_FLIP_SUMMON_TURN),TRUE);
   c->summon_player=c->current.controler;
   c->summon_info=summon==EDITOR_SUMMON_NORMAL?(SUMMON_TYPE_NORMAL|(LOCATION_HAND<<16)):(summon==EDITOR_SUMMON_SPECIAL?SUMMON_TYPE_SPECIAL:(SUMMON_TYPE_FLIP|(LOCATION_MZONE<<16)));
  }
 }
}

void TaggedEffect(duel* d,card* c,uint32_t code,uint32_t value,uint32_t kind) {
 auto* e=d->new_effect();e->owner=c;e->handler=c;e->type=EFFECT_TYPE_SINGLE;e->code=code;e->value=int32_t(value);e->editor_kind=kind;
 e->flag[0]=EFFECT_FLAG_CANNOT_DISABLE|EFFECT_FLAG_UNCOPYABLE|EFFECT_FLAG_IGNORE_IMMUNE;e->reset_flag=RESET_EVENT|RESETS_STANDARD;c->add_effect(e);
}
void ClearTagged(card* c,uint32_t kind) {std::vector<effect*> remove;for(auto& entry:c->single_effect)if(entry.second->editor_kind==kind)remove.push_back(entry.second);for(auto* e:remove)c->remove_effect(e);}
bool NativeTrap(duel* d,card_data& data,uint32_t& flags,uint32_t* nativeType=nullptr,uint32_t* summonValue=nullptr) {
 if(!(data.type&TYPE_TRAP))return false;
 char path[64]{};std::snprintf(path,sizeof path,"./script/c%u.lua",data.code);int length=0;auto* buf=read_script(path,&length);if(!buf || length<=0)return false;
 std::string source(reinterpret_cast<char*>(buf),length);const char* prefix="AddMonsterAttribute(";std::smatch transform;
 if(!std::regex_search(source,transform,std::regex("([A-Za-z_][A-Za-z_0-9]*):AddMonsterAttribute\\(")))return false;
 std::string receiver=transform[1];auto p=source.find(prefix,transform.position());std::smatch summon;
 std::regex summonPattern("Duel\\.SpecialSummon(Step)?\\(\\s*"+receiver+"\\s*,\\s*([^,]+),");if(!std::regex_search(source,summon,summonPattern))return false;
 std::string value=summon[2];value.erase(std::remove_if(value.begin(),value.end(),[](char c){return c==' ' || c=='\t' || c=='\r' || c=='\n';}),value.end());uint32_t ownValue=0;if(value=="SUMMON_VALUE_SELF")ownValue=1;else if(value!="0")return false;

 auto end=source.find(')',p);if(end==std::string::npos)return false;auto args=source.substr(p+std::strlen(prefix),end-p-std::strlen(prefix));
 static const std::regex constantsOnly("((TYPE|ATTRIBUTE|RACE)_[A-Z0-9_]+|0[xX][0-9a-fA-F]+|[0-9]+|[+|, \t\r\n-])+" );if(!std::regex_match(args,constantsOnly))return false;
 auto* lua=d->lua;auto* state=lua->current_state;int stack=lua_gettop(state);std::string expression="return "+args;
 bool valid=luaL_loadbufferx(state,expression.c_str(),expression.size(),"trap-profile","t")==0 && lua_pcall(state,0,LUA_MULTRET,0)==0;
 int count=lua_gettop(state)-stack;int32_t values[6]{};
 if(count<1 || count>6)valid=false;
 for(int i=0;valid && i<count;++i){if(!lua_isinteger(state,stack+i+1))valid=false;else values[i]=int32_t(lua_tointeger(state,stack+i+1));}lua_settop(state,stack);if(!valid)return false;
 flags=(values[0]&TYPE_EFFECT?1U:0U)|(values[0]&TYPE_TRAP?2U:0U);
 if(nativeType)*nativeType=TYPE_MONSTER|uint32_t(values[0])|((values[0]&TYPE_TRAP)?(data.type|TYPE_TRAPMONSTER):0);if(summonValue)*summonValue=ownValue;

 if(count>1 && values[1])data.attribute=values[1];if(count>2 && values[2])data.race=values[2];if(count>3 && values[3])data.level=values[3];if(count>4 && values[4])data.attack=values[4];if(count>5 && values[5])data.defense=values[5];
 return data.race && data.attribute && data.level && data.attack>=0 && data.defense>=0;
}
void CardConfiguration(duel* d,card* c,const editor_command& cmd) {
 ClearTagged(c,1);ClearTagged(c,2);ClearTagged(c,3);
 c->editor_link_marker=cmd.version>=9 && (cmd.form==5 || cmd.form==6) && (cmd.monster_flags&TYPE_LINK)?cmd.defense:UINT32_MAX;
 c->editor_form=cmd.form;c->editor_monster_flags=cmd.monster_flags;c->editor_self_summon=cmd.self_summon;
 if(cmd.form==5 || cmd.form==6){
  uint32_t customType=cmd.monster_flags&0x7fffffffU;if(cmd.form==6 && (cmd.monster_flags&0x80000000U)){customType|=c->data.type&(TYPE_SPELL|TYPE_TRAP|TYPE_CONTINUOUS|TYPE_FIELD|TYPE_EQUIP|TYPE_QUICKPLAY|TYPE_COUNTER|TYPE_RITUAL);if(c->data.type&TYPE_TRAP)customType|=TYPE_TRAPMONSTER;}TaggedEffect(d,c,EFFECT_CHANGE_TYPE,customType,1);
  TaggedEffect(d,c,EFFECT_CHANGE_RACE,cmd.race,1);TaggedEffect(d,c,EFFECT_CHANGE_ATTRIBUTE,cmd.attribute,1);
  TaggedEffect(d,c,(cmd.monster_flags&TYPE_XYZ)?EFFECT_CHANGE_RANK:EFFECT_CHANGE_LEVEL,cmd.level,1);
  TaggedEffect(d,c,EFFECT_SET_BASE_ATTACK,cmd.attack,1);if(cmd.form==5)TaggedEffect(d,c,EFFECT_SET_ATTACK_FINAL,cmd.attack,1);
  if(!(cmd.monster_flags&TYPE_LINK)){TaggedEffect(d,c,EFFECT_SET_BASE_DEFENSE,cmd.defense,1);if(cmd.form==5)TaggedEffect(d,c,EFFECT_SET_DEFENSE_FINAL,cmd.defense,1);}
  if(cmd.self_summon)TaggedEffect(d,c,EFFECT_CHANGE_CODE,cmd.self_summon,1);
 }
 else if(cmd.form==1 || cmd.form==2)TaggedEffect(d,c,EFFECT_CHANGE_TYPE,(cmd.form==1?TYPE_SPELL:TYPE_TRAP)|TYPE_CONTINUOUS,1);
 else if(cmd.form==3 || cmd.form==4) {
  uint32_t flags=cmd.monster_flags,nativeType=0,ownSummonValue=0;auto profile=c->data;if(cmd.form==4){NativeTrap(d,profile,flags,&nativeType,&ownSummonValue);c->editor_monster_flags=flags;}
  uint32_t type=TYPE_MONSTER|(flags&1?TYPE_EFFECT:TYPE_NORMAL);
  if(flags&2){type|=c->data.type&(TYPE_SPELL|TYPE_TRAP|TYPE_CONTINUOUS|TYPE_FIELD|TYPE_EQUIP|TYPE_QUICKPLAY|TYPE_COUNTER|TYPE_RITUAL);if(c->data.type&TYPE_TRAP)type|=TYPE_TRAPMONSTER;}
  if(cmd.form==4)type=nativeType;
  TaggedEffect(d,c,EFFECT_CHANGE_TYPE,type,1);
  TaggedEffect(d,c,EFFECT_CHANGE_RACE,cmd.form==4?profile.race:cmd.race,1);
  TaggedEffect(d,c,EFFECT_CHANGE_ATTRIBUTE,cmd.form==4?profile.attribute:cmd.attribute,1);
  TaggedEffect(d,c,EFFECT_CHANGE_LEVEL,cmd.form==4?profile.level:cmd.level,1);
  TaggedEffect(d,c,EFFECT_SET_BASE_ATTACK,cmd.form==4?profile.attack:cmd.attack,1);
  TaggedEffect(d,c,EFFECT_SET_BASE_DEFENSE,cmd.form==4?profile.defense:cmd.defense,1);
  if(cmd.self_summon){c->summon_info=SUMMON_TYPE_SPECIAL|ownSummonValue|(LOCATION_SZONE<<16);c->summon_player=c->current.controler;c->current.reason=REASON_EFFECT;c->current.reason_player=c->current.controler;for(const auto& e:c->field_effect)if(e.second->type&EFFECT_TYPE_ACTIVATE){c->current.reason_effect=e.second;break;}}else{c->current.reason=0;c->current.reason_effect=nullptr;c->current.reason_player=PLAYER_NONE;}
 }
 c->set_status(EDITOR_STATE_MASK,FALSE);c->set_status((cmd.states | ((cmd.value&14U)?STATUS_CANNOT_CHANGE_FORM:0))&EDITOR_STATE_MASK,TRUE);if(cmd.states&STATUS_FORBIDDEN)TaggedEffect(d,c,EFFECT_FORBIDDEN,1,3);
 for(uint32_t i=0;i<32;++i)if(cmd.restrictions&(1U<<i))TaggedEffect(d,c,editor_rule_codes[i],1,2);
}

void DeleteCard(duel* d,card* c) {
 auto materials=c->xyz_materials;
 for(auto* material:materials)DeleteCard(d,material);
 c->unequip();
 auto equipped=c->equiping_cards;
 for(auto* equip:equipped)equip->unequip();
 c->clear_card_target();c->clear_relate_effect();
 c->enable_field_effect(false);
 if(c->overlay_target)c->overlay_target->xyz_remove(c);
 else d->game_field->remove_card(c);
 c->reset(RESET_LEAVE|RESET_OVERLAY|RESET_TOHAND|RESET_TODECK|RESET_TOGRAVE|RESET_REMOVE,RESET_EVENT);
 // Keep the detached object alive: effects/groups/scripts can still hold card references.
}
}
int editor_native_trap_profile(uint32_t code,editor_card_info* output){auto h=create_duel(0);auto* d=reinterpret_cast<duel*>(h);card_data data{};read_card(code,&data);uint32_t flags=0,type=0;bool valid=NativeTrap(d,data,flags,&type);if(valid && output){*output=editor_card_info{};output->code=code;output->effective_type=type;output->race=data.race;output->attribute=data.attribute;output->level=data.level;output->attack=data.attack;output->defense=data.defense;output->monster_flags=flags;}end_duel(h);return valid;}
uint32_t editor_list_cards(intptr_t handle,editor_card_info* output,uint32_t capacity) {
 if(!handle)return 0;
 auto* d=reinterpret_cast<duel*>(handle);std::vector<card*> cards;
 for(auto* c:d->cards)if(c->current.location && c->data.code)cards.push_back(c);
 std::sort(cards.begin(),cards.end(),[](const card* a,const card* b){return a->cardid<b->cardid;});
 for(uint32_t i=0;output && i<cards.size() && i<capacity;++i) {
  const auto* c=cards[i];auto& info=output[i];info.id=c->cardid;info.code=c->data.code;info.type=c->data.type;
  info.owner=c->owner;info.player=c->overlay_target?c->overlay_target->current.controler:c->current.controler;
  info.location=c->current.location;info.sequence=c->current.sequence;info.position=c->current.position;
  info.summon_method=(c->summon_info&SUMMON_TYPE_SPECIAL)?1:(c->summon_info&SUMMON_TYPE_NORMAL)?2:(c->summon_info&SUMMON_TYPE_FLIP)?3:0;info.summon_source=(c->summon_info>>16)&0xff;info.flags=CardFlags(c);info.pzone=c->current.pzone;info.effective_type=const_cast<card*>(c)->get_type();info.actual_status=c->status;info.effective_code=const_cast<card*>(c)->get_code();
  bool configured=false;for(const auto& e:c->single_effect)if(e.second->editor_kind==1)configured=true;
  info.form=configured?c->editor_form:0;info.race=const_cast<card*>(c)->get_race();info.attribute=const_cast<card*>(c)->get_attribute();info.attack=const_cast<card*>(c)->get_attack();info.defense=const_cast<card*>(c)->get_defense();info.level=info.effective_type&TYPE_XYZ?const_cast<card*>(c)->get_rank():info.effective_type&TYPE_LINK?const_cast<card*>(c)->get_link():const_cast<card*>(c)->get_level();info.link_marker=const_cast<card*>(c)->get_link_marker();info.monster_flags=c->editor_monster_flags;info.self_summon=c->editor_self_summon;info.states=c->status&EDITOR_STATE_MASK;info.restrictions=0;
  for(const auto& e:c->single_effect)if(e.second->editor_kind==2)for(uint32_t j=0;j<32;++j)if(editor_rule_codes[j]==e.second->code)info.restrictions|=1U<<j;
 }
 return uint32_t(cards.size());
}
uint32_t editor_list_materials(intptr_t handle,editor_material_info* output,uint32_t capacity){if(!handle)return 0;uint32_t count=0;auto* d=reinterpret_cast<duel*>(handle);std::vector<card*> hosts;for(auto* c:d->cards)if(c->current.location==LOCATION_MZONE)hosts.push_back(c);std::sort(hosts.begin(),hosts.end(),[](card* a,card* b){return a->cardid<b->cardid;});for(auto* host:hosts)for(auto* material:host->xyz_materials){if(output && count<capacity)output[count]={host->cardid,material->cardid,material->data.code,material->owner};++count;}return count;}
uint32_t editor_list_counters(intptr_t handle,editor_counter_info* output,uint32_t capacity){if(!handle)return 0;uint32_t count=0;auto* d=reinterpret_cast<duel*>(handle);std::vector<card*> cards;for(auto* c:d->cards)if(c->current.location&LOCATION_ONFIELD)cards.push_back(c);std::sort(cards.begin(),cards.end(),[](card* a,card* b){return a->cardid<b->cardid;});for(auto* c:cards)for(const auto& entry:c->counters){if(output && count<capacity)output[count]={c->cardid,entry.first,entry.second};++count;}return count;}
uint32_t editor_list_custom_effects(intptr_t handle,editor_effect_info* output,uint32_t capacity){if(!handle)return 0;uint32_t count=0;auto* d=reinterpret_cast<duel*>(handle);for(auto* c:d->cards)if(c->current.location)for(const auto& e:c->single_effect)if(e.second->editor_kind==4){if(output && count<capacity)output[count]={c->cardid,e.second->code,e.second->value};++count;}return count;}
int editor_setup_decks(intptr_t handle,const uint32_t* main0,uint32_t count0,const uint32_t* extra0,uint32_t extraCount0,const uint32_t* main1,uint32_t count1,const uint32_t* extra1,uint32_t extraCount1,uint32_t hand,uint32_t hand1) {
 if(hand1==0xffffffffU)hand1=hand;
 if(!handle || count0>254 || count1>254 || extraCount0>254 || extraCount1>254 || hand>254 || hand1>254)return 0;
 auto* d=reinterpret_cast<duel*>(handle);
 if(!d->editor_enabled || d->cards.size()!=1 || !d->cards.count(d->game_field->temp_card) || !d->game_field->core.units.empty())return 0;
 const uint32_t* main[]={main0,main1};const uint32_t* extra[]={extra0,extra1};uint32_t counts[]={count0,count1},extraCounts[]={extraCount0,extraCount1};
 std::ostringstream s;s<<"Debug.SetAIName('Player 2')\nDebug.ReloadFieldBegin(DUEL_ATTACK_FIRST_TURN,5)\n";
 for(int p=0;p<2;++p) {
  if((counts[p] && !main[p]) || (extraCounts[p] && !extra[p]))return 0;
  for(uint32_t i=0;i<counts[p]+extraCounts[p];++i){card_data data{};uint32_t code=i<counts[p]?main[p][i]:extra[p][i-counts[p]];read_card(code,&data);if(!data.code || !data.type || (data.type&TYPE_TOKEN) || (i<counts[p] && (data.type&TYPES_EXTRA_DECK)) || (i>=counts[p] && !(data.type&TYPES_EXTRA_DECK)))return 0;}
  const uint32_t opening=std::min(p?hand1:hand,counts[p]);
  s<<"Debug.SetPlayerInfo("<<p<<",8000,"<<(d->editor_opening?opening:0)<<",1)\n";
  const uint32_t drawn=d->editor_opening?0:opening;
  for(uint32_t i=0;i<drawn;++i)s<<"Debug.AddCard("<<main[p][i]<<","<<p<<","<<p<<",LOCATION_HAND,0,POS_FACEDOWN_DEFENSE)\n";
  for(uint32_t i=counts[p];i>drawn;--i)s<<"Debug.AddCard("<<main[p][i-1]<<","<<p<<","<<p<<",LOCATION_DECK,0,POS_FACEDOWN_DEFENSE)\n";
  for(uint32_t i=0;i<extraCounts[p];++i)s<<"Debug.AddCard("<<extra[p][i]<<","<<p<<","<<p<<",LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE)\n";
 }
 s<<"Debug.ReloadFieldEnd()\n";auto text=s.str();auto* lua=d->lua;++lua->no_action;
 int error=luaL_loadbufferx(lua->current_state,text.c_str(),text.size(),"editor-decks","t") || lua_pcall(lua->current_state,0,0,0);
 if(error){interpreter::sprintf(d->strbuffer,"%s",lua_tostring(lua->current_state,-1));handle_message(d,1);lua_pop(lua->current_state,1);}
 --lua->no_action;return error?0:1;
}
int editor_apply(intptr_t handle,const editor_command* command) {
 if(!handle || !command)return EDITOR_BAD_COMMAND;
 auto* d=reinterpret_cast<duel*>(handle);
 if(!d->editor_enabled)return EDITOR_DISABLED;
 if(!editor_can_edit(handle))return EDITOR_UNSAFE;
 const auto& cmd=*command;
 if((cmd.version!=1 && cmd.version!=2 && cmd.version!=3 && cmd.version!=4 && cmd.version!=5 && cmd.version!=6 && cmd.version!=7 && cmd.version!=8 && cmd.version!=9) || cmd.player>1 || cmd.owner>1)return EDITOR_BAD_COMMAND;
 if(cmd.revision!=d->editor_revision)return EDITOR_STALE;
 auto* f=d->game_field;card* target=nullptr;
 if(cmd.opcode==3 || cmd.opcode==4 || cmd.opcode==5 || cmd.opcode==6 || cmd.opcode==9 || cmd.opcode==10) {
  if(cmd.version<2)return EDITOR_BAD_COMMAND;
  const uint64_t id=uint64_t(cmd.target_low)|(uint64_t(cmd.target_high)<<32);
  for(auto* c:d->cards)if(c->cardid==id && c->current.location){target=c;break;}
  if(!target)return EDITOR_BAD_COMMAND;
 }
 if(cmd.opcode==9){
  if(cmd.version<9 || target->current.location!=LOCATION_MZONE || !(target->get_type()&TYPE_XYZ) || !cmd.value || cmd.value>255 || target->xyz_materials.size()+cmd.value>255)return EDITOR_BAD_DESTINATION;
  card_data material{};read_card(cmd.code,&material);if(!material.code || material.code!=cmd.code || !material.type || (material.type&TYPE_TOKEN))return EDITOR_UNKNOWN_CARD;
  for(uint32_t i=0;i<cmd.value;++i){auto* c=d->new_card(cmd.code);c->owner=uint8_t(cmd.owner);target->xyz_add(c);}
 } else if(cmd.opcode==10){
  if(cmd.version<9 || !(target->current.location&LOCATION_ONFIELD) || !(target->current.position&POS_FACEUP) || !cmd.code || cmd.code>65535 || cmd.value>65535)return EDITOR_BAD_DESTINATION;
  // State editing deliberately bypasses placement permissions and counter limits.
  // Real scripts read/remove these counters through the ordinary card counter map.
  if(cmd.value)target->counters[uint16_t(cmd.code)]=uint16_t(cmd.value);else target->counters.erase(uint16_t(cmd.code));
 } else if(cmd.opcode==8){if(cmd.version<9 || !cmd.value || cmd.value>255)return EDITOR_BAD_COMMAND;f->draw(nullptr,REASON_RULE,cmd.player,cmd.player,cmd.value);
 } else if(cmd.opcode==7) {
  if(cmd.version<5 || (cmd.location!=LOCATION_DECK && cmd.location!=LOCATION_EXTRA))return EDITOR_BAD_DESTINATION;
  auto& list=cmd.location==LOCATION_DECK?f->player[cmd.player].list_main:f->player[cmd.player].list_extra;
  auto shuffle=[&](size_t begin,size_t end){for(size_t i=end;i>begin+1;--i){size_t j=size_t(d->get_next_integer(int(begin),int(i-1)));std::swap(list[i-1],list[j]);}};
  if(cmd.location==LOCATION_EXTRA){size_t boundary=list.size()-f->player[cmd.player].extra_p_count;shuffle(0,boundary);shuffle(boundary,list.size());}else shuffle(0,list.size());
  for(size_t i=0;i<list.size();++i)list[i]->current.sequence=uint8_t(i);
 } else if(cmd.opcode==2) {
  if(!cmd.value || cmd.value>uint32_t(std::numeric_limits<int32_t>::max()))return EDITOR_BAD_COMMAND;
  f->player[cmd.player].lp=int32_t(cmd.value);
 } else if(cmd.opcode==6) {
  if(std::find(std::begin(editor_catalog_codes),std::end(editor_catalog_codes),cmd.code)==std::end(editor_catalog_codes) || cmd.position>1)return EDITOR_BAD_COMMAND;
  std::vector<effect*> remove;for(const auto& e:target->single_effect)if(e.second->editor_kind==4 && e.second->code==cmd.code)remove.push_back(e.second);for(auto* e:remove)target->remove_effect(e);
  if(cmd.position)TaggedEffect(d,target,cmd.code,cmd.value,4);
 } else if(cmd.opcode==5) {
  if(target->current.controler!=cmd.player || (target->current.location!=LOCATION_DECK && target->current.location!=LOCATION_EXTRA))return EDITOR_BAD_DESTINATION;
  auto& list=target->current.location==LOCATION_DECK?f->player[cmd.player].list_main:f->player[cmd.player].list_extra;
  if(cmd.sequence>=list.size())return EDITOR_BAD_DESTINATION;
  if(target->current.location==LOCATION_EXTRA){size_t boundary=list.size()-f->player[cmd.player].extra_p_count;bool face=!!(target->current.position&POS_FACEUP);if((face && cmd.sequence<boundary) || (!face && cmd.sequence>=boundary))return EDITOR_BAD_DESTINATION;}
  auto it=std::find(list.begin(),list.end(),target);if(it==list.end())return EDITOR_BAD_COMMAND;
  list.erase(it);size_t index=target->current.location==LOCATION_DECK?list.size()-cmd.sequence:cmd.sequence;
  list.insert(list.begin()+index,target);for(size_t i=0;i<list.size();++i)list[i]->current.sequence=uint8_t(i);
 } else if(cmd.opcode==3) {
  auto oldShuffle0=f->core.shuffle_deck_check[0],oldShuffle1=f->core.shuffle_deck_check[1];
  DeleteCard(d,target);
  // Native removal marks a deck for automatic shuffling. An editor deletion preserves order.
  // Earlier command versions retain their recorded behavior for old saves and replays.
  if(cmd.version>=5){f->core.shuffle_deck_check[0]=oldShuffle0;f->core.shuffle_deck_check[1]=oldShuffle1;}
 } else if(cmd.opcode==1 || cmd.opcode==4) {
  card_data data{};
  if(target)data=target->data;else read_card(cmd.code,&data);
  if(!data.code || !data.type || (!target && data.code!=cmd.code))return EDITOR_UNKNOWN_CARD;
  if(target && target->current.location==LOCATION_OVERLAY)return EDITOR_BAD_DESTINATION;
  uint32_t summonSource=cmd.version>=7?((cmd.value>>8)&0xffU):0;bool specialOrigin=cmd.version>=7 && (cmd.value&EDITOR_SPECIAL_ORIGIN),normalOrigin=cmd.version>=7 && (cmd.value&EDITOR_NORMAL_ORIGIN),flipOrigin=cmd.version>=7 && (cmd.value&EDITOR_FLIP_ORIGIN);
  if(cmd.version>=7 && cmd.form==4 && cmd.self_summon){if(normalOrigin || flipOrigin)return EDITOR_BAD_COMMAND;specialOrigin=true;if(!summonSource)summonSource=LOCATION_SZONE;}
  if((int(specialOrigin)+int(normalOrigin)+int(flipOrigin)>1) || ((specialOrigin || normalOrigin || flipOrigin) && cmd.location!=LOCATION_MZONE) || (summonSource && !specialOrigin && !normalOrigin && !flipOrigin) || (normalOrigin && summonSource && summonSource!=LOCATION_HAND) || (flipOrigin && summonSource && summonSource!=LOCATION_MZONE))return EDITOR_BAD_COMMAND;
  if(specialOrigin && !summonSource)return EDITOR_BAD_COMMAND;
  if(normalOrigin)summonSource=LOCATION_HAND;if(flipOrigin)summonSource=LOCATION_MZONE;
  if(summonSource && (cmd.location!=LOCATION_MZONE || (summonSource!=LOCATION_DECK && summonSource!=LOCATION_HAND && summonSource!=LOCATION_MZONE && summonSource!=LOCATION_SZONE && summonSource!=LOCATION_GRAVE && summonSource!=LOCATION_REMOVED && summonSource!=LOCATION_EXTRA)))return EDITOR_BAD_COMMAND;
  if((cmd.value&~(cmd.version>=7?(255U|0xff00U):cmd.version>=6?31U:15U)) || ((cmd.value&EDITOR_SENT_GRAVE_TURN) && cmd.location!=LOCATION_GRAVE) || cmd.sequence>254) return EDITOR_BAD_COMMAND;
  if(cmd.form>6 || (cmd.form>=5 && cmd.version<4) || (cmd.form<5 && (cmd.monster_flags>3 || cmd.self_summon>1)) || (cmd.states&~EDITOR_STATE_MASK))return EDITOR_BAD_COMMAND;
  if((cmd.form==1 || cmd.form==2) && (!(data.type&TYPE_MONSTER) || cmd.location!=LOCATION_SZONE))return EDITOR_BAD_DESTINATION;
  if((cmd.form==3 || cmd.form==4) && (!(data.type&(TYPE_SPELL|TYPE_TRAP)) || cmd.location!=LOCATION_MZONE))return EDITOR_BAD_DESTINATION;
  if(cmd.form==3 && (!cmd.race || (cmd.race&(cmd.race-1)) || !cmd.attribute || (cmd.attribute&(cmd.attribute-1)) || !cmd.level || cmd.level>13 || cmd.attack>0x7fffffff || cmd.defense>0x7fffffff || cmd.self_summon))return EDITOR_BAD_COMMAND;
  uint32_t nativeFlags=0;if(cmd.form==4 && !NativeTrap(d,data,nativeFlags))return EDITOR_BAD_DESTINATION;
  if(cmd.form==5 || cmd.form==6){if(cmd.version>=9 && (cmd.monster_flags&TYPE_LINK) && (!cmd.defense || (cmd.defense&~0x1efU)))return EDITOR_BAD_COMMAND;const uint32_t allowed=TYPE_MONSTER|TYPE_NORMAL|TYPE_EFFECT|TYPE_FUSION|TYPE_RITUAL|TYPE_SPIRIT|TYPE_UNION|TYPE_DUAL|TYPE_TUNER|TYPE_SYNCHRO|TYPE_FLIP|TYPE_TOON|TYPE_XYZ|TYPE_PENDULUM|TYPE_SPSUMMON|TYPE_LINK|(cmd.form==6?0x80000000U:0);
   if((cmd.form==5?!(data.type&TYPE_MONSTER):!(data.type&(TYPE_SPELL|TYPE_TRAP))) || cmd.location!=LOCATION_MZONE || !(cmd.monster_flags&TYPE_MONSTER) || (cmd.monster_flags&~allowed) || (cmd.version<9?(!!(cmd.monster_flags&TYPE_NORMAL)==!!(cmd.monster_flags&TYPE_EFFECT)):((cmd.monster_flags&TYPE_EFFECT)?bool(cmd.monster_flags&TYPE_NORMAL):bool(cmd.monster_flags&(TYPE_RITUAL|TYPES_EXTRA_DECK))?bool(cmd.monster_flags&TYPE_NORMAL):!(cmd.monster_flags&TYPE_NORMAL))) || !cmd.race || (cmd.race&(cmd.race-1)) || !cmd.attribute || (cmd.attribute&(cmd.attribute-1)) || !cmd.level || cmd.level>13 || cmd.attack>0x7fffffff || cmd.defense>0x7fffffff)return EDITOR_BAD_COMMAND;
   if(cmd.self_summon){card_data name{};read_card(cmd.self_summon,&name);if(!name.code || !(name.type&7))return EDITOR_UNKNOWN_CARD;}
  }
  const uint32_t summon=cmd.value&14U;
  if(summon && summon!=EDITOR_SUMMON_NORMAL && summon!=EDITOR_SUMMON_SPECIAL && summon!=EDITOR_SUMMON_FLIP) return EDITOR_BAD_COMMAND;
  if(summon && (cmd.location!=LOCATION_MZONE || ((summon==EDITOR_SUMMON_FLIP || (summon==EDITOR_SUMMON_NORMAL && cmd.version<7)) && !(cmd.position&POS_FACEUP)))) return EDITOR_BAD_DESTINATION;
  uint32_t loc=cmd.location, seq=cmd.sequence, pos=cmd.position;
  bool pzone=false;
  if(pos!=POS_FACEUP_ATTACK && pos!=POS_FACEUP_DEFENSE && pos!=POS_FACEDOWN_DEFENSE) return EDITOR_BAD_DESTINATION;
  const bool extra=!!(data.type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK));
  bool editorShape=false;if(target)for(const auto& e:target->single_effect)if(e.second->editor_kind==1)editorShape=true;
  const uint32_t originalEffective=target && !editorShape?target->get_type():data.type;

  uint32_t editedType=cmd.form>=5?(cmd.monster_flags&0x7fffffffU):cmd.form==3?(TYPE_MONSTER|((cmd.monster_flags&1)?TYPE_EFFECT:TYPE_NORMAL)):data.type;
  if(cmd.form==6 && (cmd.monster_flags&0x80000000U))editedType|=data.type&(TYPE_SPELL|TYPE_TRAP|TYPE_CONTINUOUS|TYPE_FIELD|TYPE_EQUIP|TYPE_QUICKPLAY|TYPE_COUNTER|TYPE_RITUAL);
  bool trapMonster=(data.type&TYPE_TRAP) && (cmd.form==3 || cmd.form==4 || cmd.form==6 || (originalEffective&TYPE_MONSTER));
  if(cmd.version>=8 && trapMonster && (cmd.location!=LOCATION_MZONE || cmd.sequence>=5 || summonSource==LOCATION_EXTRA || normalOrigin || (cmd.value&14U)==EDITOR_SUMMON_NORMAL))return EDITOR_BAD_DESTINATION;
  if(normalOrigin && !editor_allows_normal_summon(editedType))return EDITOR_BAD_DESTINATION;
  if(flipOrigin && ((editedType&(TYPE_LINK|TYPE_TOKEN)) || !(cmd.position&POS_FACEUP)))return EDITOR_BAD_DESTINATION;
  if((normalOrigin && (summon==EDITOR_SUMMON_SPECIAL || summon==EDITOR_SUMMON_FLIP)) || (specialOrigin && (summon==EDITOR_SUMMON_NORMAL || summon==EDITOR_SUMMON_FLIP)) || (flipOrigin && (summon==EDITOR_SUMMON_NORMAL || summon==EDITOR_SUMMON_SPECIAL)))return EDITOR_BAD_COMMAND;
  if(summonSource==LOCATION_EXTRA && !(editedType&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK|TYPE_PENDULUM)))return EDITOR_BAD_DESTINATION;
  if(summon==EDITOR_SUMMON_NORMAL && !editor_allows_normal_summon(editedType)) return EDITOR_BAD_DESTINATION;
  if(summon==EDITOR_SUMMON_FLIP && (editedType&(TYPE_LINK|TYPE_TOKEN))) return EDITOR_BAD_DESTINATION;
  if((data.type&TYPE_TOKEN) && (pos&POS_FACEDOWN)) return EDITOR_BAD_DESTINATION;
  switch(loc) {
  case LOCATION_HAND: case LOCATION_DECK:
   if(extra || (data.type&TYPE_TOKEN)) return EDITOR_BAD_DESTINATION;
   if(loc==LOCATION_DECK && seq>(cmd.version>=4?2U:SEQ_DECKBOTTOM)) return EDITOR_BAD_DESTINATION;
   pos=POS_FACEDOWN_DEFENSE;break;
  case LOCATION_EXTRA:
   if((!extra && !(data.type&TYPE_PENDULUM)) || (data.type&TYPE_TOKEN)) return EDITOR_BAD_DESTINATION;
   if((pos&POS_FACEUP) && !(data.type&TYPE_PENDULUM)) return EDITOR_BAD_DESTINATION;
   if(cmd.version>=6 && (seq>2 || ((data.type&TYPE_PENDULUM) && !(data.type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ)) && !(pos&POS_FACEUP))))return EDITOR_BAD_DESTINATION;
   break;
  case LOCATION_GRAVE:
   if(data.type&TYPE_TOKEN) return EDITOR_BAD_DESTINATION;
   pos=POS_FACEUP_ATTACK;break;
  case LOCATION_REMOVED:
   if(data.type&TYPE_TOKEN) return EDITOR_BAD_DESTINATION;
   break;
  case LOCATION_MZONE:
   if(cmd.version>=7 && seq>=5 && summonSource!=LOCATION_EXTRA)return EDITOR_BAD_DESTINATION;
   if((!(originalEffective&TYPE_MONSTER) && cmd.form!=3 && cmd.form!=4 && cmd.form!=6) || seq>6 || ((editedType&TYPE_LINK) && pos!=POS_FACEUP_ATTACK)) return EDITOR_BAD_DESTINATION;
   break;
  case LOCATION_FZONE:
   if(!(data.type&TYPE_FIELD) || seq) return EDITOR_BAD_DESTINATION;
   loc=LOCATION_SZONE;seq=5;break;
  case LOCATION_PZONE:
   if(!(data.type&TYPE_PENDULUM) || seq>1 || pos!=POS_FACEUP_ATTACK) return EDITOR_BAD_DESTINATION;
   seq=f->get_pzone_sequence(uint8_t(seq));loc=LOCATION_SZONE;pzone=true;break;
  case LOCATION_SZONE:
   if((!(originalEffective&(TYPE_SPELL|TYPE_TRAP)) && cmd.form!=1 && cmd.form!=2) || (data.type&TYPE_FIELD) || seq>4 || pos==POS_FACEUP_DEFENSE) return EDITOR_BAD_DESTINATION;
   break;
  default:return EDITOR_BAD_DESTINATION;
  }
  if(cmd.version>=4 && loc==LOCATION_SZONE && !pzone && (pos&POS_FACEUP) && cmd.form!=1 && cmd.form!=2 && !(originalEffective&(TYPE_CONTINUOUS|TYPE_FIELD)))return EDITOR_BAD_DESTINATION;
  if(loc==LOCATION_SZONE && pos==POS_FACEUP_DEFENSE) return EDITOR_BAD_DESTINATION;
  if(!(target && target->current.controler==cmd.player && target->current.location==loc && target->current.sequence==seq) && !f->is_location_useable(uint8_t(cmd.player),uint8_t(loc),uint8_t(seq))) return EDITOR_OCCUPIED;
  if(!(target && target->current.location==loc && target->current.controler==cmd.player) && !(loc&LOCATION_ONFIELD) && query_field_count(handle,uint8_t(cmd.player),uint8_t(loc))>=254) return EDITOR_BAD_DESTINATION;

  if(target && (target->overlay_target || target->equiping_target || !target->xyz_materials.empty() || !target->equiping_cards.empty())
     && (target->current.location!=loc || target->current.controler!=cmd.player))return EDITOR_BAD_DESTINATION;
  card* c=target?target:d->new_card(cmd.code);
  uint32_t oldSummonInfo=c->summon_info,oldTurn=c->turnid,oldSummonPlayer=c->summon_player;
  bool keepSummonHistory=target && (cmd.form==0 || cmd.form>=5) && target->current.location==LOCATION_MZONE && loc==LOCATION_MZONE && (CardFlags(target)&14U)==(cmd.value&14U);

  const bool moved=target && (target->current.location!=loc || target->current.controler!=cmd.player || ((loc&LOCATION_ONFIELD) && target->current.sequence!=seq) || loc==LOCATION_DECK || (cmd.version>=6 && loc==LOCATION_EXTRA));
  if(moved) {
   c->enable_field_effect(false);
   if(c->current.location!=loc)c->reset(RESET_LEAVE|RESET_TOFIELD|RESET_TOHAND|RESET_TODECK|RESET_TOGRAVE|RESET_REMOVE,RESET_EVENT);
   f->remove_card(c);
  }
  c->owner=uint8_t(cmd.owner);c->sendto_param.position=uint8_t(pos);
  auto hand_shuffle=f->core.shuffle_hand_check[cmd.player];auto deck_shuffle=f->core.shuffle_deck_check[cmd.player];
  if(!target || moved)f->add_card(uint8_t(cmd.player),c,uint8_t(loc),uint8_t(seq),pzone);
  // Update the face-up Pendulum count if changing an Extra Deck card in place.
  if(target && !moved && loc==LOCATION_EXTRA && (data.type&TYPE_PENDULUM)) {
   bool oldFace=!!(c->current.position&POS_FACEUP),newFace=!!(pos&POS_FACEUP);
   if(oldFace!=newFace){c->enable_field_effect(false);f->remove_card(c);f->add_card(cmd.player,c,loc,0,pzone);}
  }
  if(cmd.version>=4 && loc==LOCATION_DECK && seq==2){auto& deck=f->player[cmd.player].list_main;auto it=std::find(deck.begin(),deck.end(),c);deck.erase(it);size_t index=d->get_next_integer(0,int(deck.size()));deck.insert(deck.begin()+index,c);for(size_t i=0;i<deck.size();++i)deck[i]->current.sequence=uint8_t(i);}
  if(cmd.version>=6 && loc==LOCATION_EXTRA){auto& extraList=f->player[cmd.player].list_extra;auto it=std::find(extraList.begin(),extraList.end(),c);extraList.erase(it);bool face=!!(pos&POS_FACEUP);size_t remainingFace=f->player[cmd.player].extra_p_count-(face?1:0),boundary=extraList.size()-remainingFace;size_t begin=face?boundary:0,end=face?extraList.size():boundary;size_t index=seq==0?begin:end;if(seq==2)index=size_t(d->get_next_integer(int(end-begin>=2?begin+1:begin),int(end-begin>=2?end-1:end)));extraList.insert(extraList.begin()+index,c);for(size_t i=0;i<extraList.size();++i)extraList[i]->current.sequence=uint8_t(i);}
  f->core.shuffle_hand_check[cmd.player]=hand_shuffle;f->core.shuffle_deck_check[cmd.player]=deck_shuffle;
  c->current.position=uint8_t(pos);c->current.pzone=pzone;
  ControlOverride(d,c,uint8_t(cmd.player));
  c->enable_field_effect(!(loc&LOCATION_ONFIELD) || !!(pos&POS_FACEUP));
  TurnState(f,c,cmd.value);
  if(cmd.version>=3)CardConfiguration(d,c,cmd);
  if(cmd.version>=6 && loc==LOCATION_GRAVE)c->turnid=(cmd.value&EDITOR_SENT_GRAVE_TURN)?f->infos.turn_id:(f->infos.turn_id?f->infos.turn_id-1:std::numeric_limits<uint16_t>::max());
  if(keepSummonHistory){c->summon_info=oldSummonInfo;c->turnid=oldTurn;c->summon_player=oldSummonPlayer;}
  if(cmd.version>=7 && loc==LOCATION_MZONE){if(specialOrigin || normalOrigin || flipOrigin){uint32_t methodType=specialOrigin?SUMMON_TYPE_SPECIAL:normalOrigin?SUMMON_TYPE_NORMAL:SUMMON_TYPE_FLIP;if(cmd.form==4 && specialOrigin)methodType=cmd.self_summon?(c->summon_info&~0xff0000U):SUMMON_TYPE_SPECIAL;bool unchanged=target && (oldSummonInfo&methodType) && (((oldSummonInfo>>16)&0xff)==summonSource) && (cmd.form!=4 || (oldSummonInfo&0xff00ffffU)==methodType);c->summon_info=((unchanged?oldSummonInfo:methodType)&~0xff0000U)|(summonSource<<16);c->summon_player=unchanged?oldSummonPlayer:c->current.controler;}else if(!(cmd.value&14U)){c->summon_info=0;c->summon_player=PLAYER_NONE;}}
 } else return EDITOR_BAD_COMMAND;
 if(!f->core.units.empty() && (f->core.units.front().type==PROCESSOR_SELECT_IDLECMD
    || f->core.units.front().type==PROCESSOR_SELECT_BATTLECMD || f->core.units.front().type==PROCESSOR_SELECT_CHAIN)){
  f->core.units.pop_front();f->core.units.front().step=0;
 }
 f->core.summonable_cards.clear();f->core.spsummonable_cards.clear();f->core.repositionable_cards.clear();
 f->core.msetable_cards.clear();f->core.ssetable_cards.clear();f->core.select_chains.clear();f->returns.ivalue[0]=0;
 d->editor_waiting=false;++d->editor_revision;f->adjust_all();f->reload_field_info();return EDITOR_OK;
}

int editor_apply_group(intptr_t handle,editor_command* commands,uint32_t count){
 if(!handle || !commands || !count || count>512 || editor_group)return EDITOR_BAD_COMMAND;
 if(!editor_can_edit(handle))return EDITOR_UNSAFE;
 for(uint32_t i=0;i<count;++i)if(commands[i].opcode!=1 && commands[i].opcode!=3 && commands[i].opcode!=4 && commands[i].opcode!=9 && commands[i].opcode!=10)return EDITOR_BAD_COMMAND;
 auto* d=reinterpret_cast<duel*>(handle);struct Guard{Guard(duel* d){editor_group=d;}~Guard(){editor_group=nullptr;}} guard(d);uint64_t created=0;
 for(uint32_t i=0;i<count;++i){auto& cmd=commands[i];cmd.revision=d->editor_revision;if((cmd.opcode==9 || cmd.opcode==10) && !cmd.target_low && !cmd.target_high){cmd.target_low=uint32_t(created);cmd.target_high=uint32_t(created>>32);}int result=editor_apply(handle,&cmd);if(result!=EDITOR_OK)return result;if(cmd.opcode==1){created=0;for(auto* c:d->cards)if(c->current.location && c->current.location!=LOCATION_OVERLAY && c->cardid>created)created=c->cardid;}}
 return EDITOR_OK;
}
