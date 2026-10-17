#define main legacy_engine_main
#include "engine_smoke.cpp"
#undef main
int main(){try{
 auto* device=irr::createDevice(irr::video::EDT_NULL);dataManager.IrrFileSystem=device->getFileSystem();check(dataManager.LoadDB("cards.cdb"),"DB");set_card_reader(DataManager::CardReader);set_script_reader(default_script_reader);set_message_handler(logMessage);
 auto h=create();auto*f=reinterpret_cast<duel*>(h)->game_field;
 editor_command opp{};opp.opcode=1;opp.code=84815190;opp.player=opp.owner=1;opp.location=LOCATION_MZONE;opp.position=POS_FACEUP_ATTACK;opp.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<8);opp.revision=editor_revision(h);check(editor_apply(h,&opp)==0,"Opponent fixture card");toIdle(h);
 opp.code=89631139;opp.location=LOCATION_HAND;opp.value=0;opp.revision=editor_revision(h);check(editor_apply(h,&opp)==0,"Opponent fixture card");toIdle(h);
 editor_command host{};host.opcode=1;host.code=88021907;host.location=LOCATION_MZONE;host.position=POS_FACEUP_ATTACK;host.value=EDITOR_SPECIAL_ORIGIN|(LOCATION_EXTRA<<8);
 editor_command kind{};kind.opcode=12;kind.value=3;editor_command mat{};mat.opcode=13;mat.code=17946349;mat.location=LOCATION_MZONE;mat.sequence=1;mat.position=POS_FACEUP_ATTACK;mat.value=LOCATION_OVERLAY;mat.level=POS_FACEUP_ATTACK;editor_command end{};end.opcode=14;auto m2=mat;m2.code=89631139;m2.sequence=2;auto m3=m2;m3.sequence=3;std::vector<editor_command> cmds{host,kind,mat,m2,m3,end};check(editor_apply_group(h,cmds.data(),cmds.size())==0,"Diameter xyz group");
 f->add_process(PROCESSOR_POINT_EVENT,0,nullptr,nullptr,0,0);bool baronneUsed=false,diameterUsed=false,laplaceUsed=false;
 for(int i=0;i<80;i++){
  pump(h);auto u=f->core.units.front();std::cout<<"BOUNDARY "<<u.type<<" p="<<u.arg1<<" chains="<<f->core.current_chain.size()<<std::endl;
  for(auto&ch:f->core.current_chain)std::cout<<" CH "<<int(ch.chain_count)<<" code="<<ch.triggering_effect->handler->data.code<<" desc="<<ch.triggering_effect->description<<" ev="<<ch.evt.event_value<<" flag="<<ch.flag<<std::endl;
  if(u.type==PROCESSOR_SELECT_IDLECMD)break;
  if(u.type==PROCESSOR_SELECT_EFFECTYN){response(h,1);laplaceUsed=true;continue;}
  if(u.type==PROCESSOR_SELECT_CHAIN){int select=-1;for(size_t j=0;j<f->core.select_chains.size();j++){auto&e=f->core.select_chains[j];auto*c=e.triggering_effect->handler;std::cout<<" OPT "<<j<<" code="<<c->data.code<<" desc="<<e.triggering_effect->description<<" ev="<<e.evt.event_value<<" ep="<<e.evt.event_player<<" rp="<<e.evt.reason_player<<std::endl;if(c->data.code==84815190&&!baronneUsed){select=j;baronneUsed=true;break;}if(c->data.code==88021907&&e.triggering_effect->code==EVENT_CHAINING&&!diameterUsed){select=j;diameterUsed=true;break;}if(c->data.code==88021907&&e.triggering_effect->code==EVENT_SPSUMMON_SUCCESS&&!laplaceUsed){select=j;laplaceUsed=true;break;}}response(h,select);continue;}
  if(u.type==PROCESSOR_SELECT_CARD){std::array<uint8_t,SIZE_RETURN_VALUE>a{};a[0]=1;a[1]=0;responseBytes(h,a.data(),2);continue;}
  if(u.type==PROCESSOR_SELECT_PLACE){std::array<uint8_t,SIZE_RETURN_VALUE>a{};a[0]=0;a[1]=LOCATION_SZONE;a[2]=0;responseBytes(h,a.data(),3);continue;}
  if(u.type==PROCESSOR_SELECT_OPTION){response(h,0);continue;}
  if(u.type==PROCESSOR_SELECT_YESNO){response(h,0);continue;}
  throw std::runtime_error("Unknown boundary "+std::to_string(u.type));
 }
 std::cout<<"USED "<<laplaceUsed<<baronneUsed<<diameterUsed<<" opphand="<<f->player[1].list_hand.size()<<" ownm="<<(f->player[0].list_mzone[0]!=nullptr)<<std::endl;check(laplaceUsed&&baronneUsed&&diameterUsed,"Actual Laplacian / opponent Baronne / Diameter-granted response form three chain links");check(f->player[1].list_hand.empty(),"Laplacian first effect resolves after granted effect negates opponent Baronne");check(f->player[0].list_mzone[0]&&f->player[0].list_mzone[0]->data.code==88021907,"Laplacian remains on own field");check(scriptErrors==0,"No errors in actual Diameter and Laplacian scripts");std::cout<<"SUCCESS Diameter native three-link regression"<<std::endl;end_duel(h);return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
