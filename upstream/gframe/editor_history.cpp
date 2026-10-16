#include "editor_history.h"
#include "replay.h"
#include "../ocgcore/duel.h"
#include "../ocgcore/field.h"
#include "../ocgcore/card.h"
#include "../ocgcore/interpreter.h"
#include "../ocgcore/effect.h"
#include <algorithm>
#include <cstring>
#include <sstream>
namespace ygo {
void EditorHistory::Reset(const uint32_t* seeds,const std::string& name,uint32_t opt) {
 std::copy(seeds,seeds+SEED_COUNT,seed.begin());script=name;options=opt;
 inputs.clear();checkpoints.clear();customSetup=false;
}
void EditorHistory::RecordResponse(const unsigned char* data,unsigned int length,size_t bytes) {
 if(!data || !length || length>SIZE_RETURN_VALUE)return;
 Input entry{};entry.length=length;entry.recordedBytes=bytes;
 std::memcpy(entry.response.data(),data,length);inputs.push_back(entry);
}
void EditorHistory::RecordEdit(const editor_command& command,size_t bytes) {
 Input entry{};entry.edit=true;entry.command=command;entry.recordedBytes=bytes;inputs.push_back(entry);
}
std::string EditorHistory::State(intptr_t h,bool extended) {
 auto* d=reinterpret_cast<duel*>(h);auto* f=d->game_field;
 std::ostringstream extra;
 extra<<f->infos.turn_id<<':'<<int(f->infos.turn_player)<<':'<<f->infos.phase<<':'<<d->editor_revision;
 for(int p=0;p<2;++p)extra<<':'<<f->core.summon_count[p]<<':'<<int(f->core.extra_summon[p]);
 for(int p=0;p<3;++p) {
  for(const auto* map:{&f->core.effect_count_code[p],&f->core.effect_count_code_duel[p],&f->core.effect_count_code_chain[p]}) {
   std::vector<std::pair<uint32_t,int32_t>> counts(map->begin(),map->end());std::sort(counts.begin(),counts.end());
   extra<<'|';for(const auto& entry:counts)extra<<entry.first<<','<<entry.second<<';';
  }
 }
 for(int player=0;player<2;++player)for(auto loc:{LOCATION_DECK,LOCATION_HAND,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE,LOCATION_REMOVED,LOCATION_EXTRA})for(int index=0;index<255;++index) {
  auto* card=f->get_field_card(player,loc,index);
  if(card)extra<<'/'<<player<<','<<loc<<','<<index<<','<<card->status<<','<<card->turnid<<','<<card->summon_info<<','<<int(card->summon_player);
 }
 if(extended){for(const auto& unit:f->core.subunits)extra<<"S"<<unit.type<<","<<unit.step<<","<<unit.arg1<<","<<unit.arg2<<","<<unit.arg3<<","<<unit.arg4<<","<<(unit.peffect?unit.peffect->id:0);std::vector<card*> sorted(d->cards.begin(),d->cards.end());std::sort(sorted.begin(),sorted.end(),[](card* a,card* b){return a->cardid<b->cardid;});for(auto* c:sorted)if(c->current.location){for(const auto& counter:c->counters)extra<<"C"<<c->cardid<<","<<counter.first<<","<<counter.second;for(auto* m:c->xyz_materials)extra<<"M"<<c->cardid<<","<<m->cardid<<","<<m->data.code<<","<<int(m->owner);}}
 extra<<d->random.state();extra<<":"<<d->lua->call_depth<<":"<<d->lua->coroutines.size();
 for(const auto& unit:f->core.units)extra<<"U"<<unit.type<<","<<unit.step<<","<<unit.arg1<<","<<unit.arg2<<","<<unit.arg3<<","<<unit.arg4<<","<<(unit.peffect?unit.peffect->id:0);
 std::vector<effect*> effects(d->effects.begin(),d->effects.end());std::sort(effects.begin(),effects.end(),[](const effect* a,const effect* b){return a->id<b->id;});
 for(auto* e:effects)extra<<"E"<<e->id<<","<<e->code<<","<<e->type<<","<<(e->is_flag(EFFECT_FLAG_FUNC_VALUE)?0:e->value)<<","<<e->flag[0]<<","<<e->flag[1]<<","<<e->reset_flag<<","<<e->reset_count<<","<<int(e->count_limit)<<","<<e->range<<","<<(e->handler?e->handler->cardid:0)<<","<<(e->owner?e->owner->cardid:0)<<","<<e->editor_kind;

 std::string state=extra.str();std::vector<uint8_t> buf(1024*1024);
 auto len=query_field_info(h,buf.data());state.append(reinterpret_cast<char*>(buf.data()),len);
 for(int p=0;p<2;++p)for(auto loc:{LOCATION_DECK,LOCATION_HAND,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE,LOCATION_REMOVED,LOCATION_EXTRA}) {
  len=query_field_card(h,p,loc,0xf81fff,buf.data(),0);
  state.append(reinterpret_cast<char*>(buf.data()),len);
 }
 return state;
}
void EditorHistory::MarkCheckpoint(intptr_t h) {
 if(!editor_can_edit(h))return;
 if(checkpoints.empty()||checkpoints.back().index!=inputs.size())checkpoints.push_back({inputs.size(),State(h)});
}
void EditorHistory::DiscardRejectedResponse() {
 if(inputs.empty()||inputs.back().edit)return;
 inputs.pop_back();
 while(!checkpoints.empty()&&checkpoints.back().index>inputs.size())checkpoints.pop_back();
}
bool EditorHistory::CanUndo() const {
 return checkpoints.size()>1&&checkpoints.back().index==inputs.size();
}
bool EditorHistory::PumpToInput(intptr_t h) {
 std::vector<uint8_t> buffer(SIZE_MESSAGE_BUFFER);
 for(int step=0;step<100000;++step) {
  uint32_t status=process(h);
  if(status&PROCESSOR_BUFFER_LEN)get_message(h,buffer.data());
  if((status&PROCESSOR_FLAG)==PROCESSOR_END)return false;
  if((status&PROCESSOR_FLAG)==PROCESSOR_WAITING)return true;
 }
 return false;
}
bool EditorHistory::Undo(intptr_t& current,Replay& replay) {
 if(!CanUndo()||!editor_can_edit(current))return false;
 const auto target=checkpoints[checkpoints.size()-2];
 intptr_t restored=create_duel_v2(seed.data());editor_enable(restored,1);
 set_player_info(restored,0,8000,5,1);set_player_info(restored,1,8000,5,1);
 std::vector<uint8_t> buffer(SIZE_MESSAGE_BUFFER);
 bool valid=customSetup?initialSetup.Initialize(restored):preload_script(restored,script.c_str())!=0;
 if(valid) {
  get_message(restored,buffer.data());start_duel(restored,options);
  valid=PumpToInput(restored);
 }
 for(size_t i=0;valid && i<target.index;++i) {
  auto& input=inputs[i];
  if(input.edit)valid=editor_apply(restored,&input.command)==EDITOR_OK;
  else set_responseb(restored,input.response.data());
  if(valid)valid=PumpToInput(restored);
 }
 if(valid)valid=editor_can_edit(restored)&&State(restored)==target.state;
 size_t removed=0;
 for(size_t i=target.index;i<inputs.size();++i)removed+=inputs[i].recordedBytes;
 if(valid && removed)valid=replay.RemoveData(removed);
 if(!valid){end_duel(restored);return false;}
 end_duel(current);current=restored;
 inputs.resize(target.index);checkpoints.pop_back();
 // Rebuild the client from the restored engine's field and already-computed action vectors.
 auto* field=reinterpret_cast<duel*>(restored)->game_field;
 field->reload_field_info();field->core.units.front().step=0;
 reinterpret_cast<duel*>(restored)->editor_waiting=false;
 return true;
}
}

