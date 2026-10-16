static uint32_t EditorSummonKind(card* c) {
 const uint32_t type=c->summon_info&0xff00ffffU;
 return type==SUMMON_TYPE_FUSION?1:type==SUMMON_TYPE_SYNCHRO?2:type==SUMMON_TYPE_XYZ?3:type==SUMMON_TYPE_LINK?4:type==SUMMON_TYPE_RITUAL?5:0;
}
static uint32_t EditorSummonType(uint32_t kind){static const uint32_t types[]={SUMMON_TYPE_SPECIAL,SUMMON_TYPE_FUSION,SUMMON_TYPE_SYNCHRO,SUMMON_TYPE_XYZ,SUMMON_TYPE_LINK,SUMMON_TYPE_RITUAL};return kind<=5?types[kind]:0;}
static bool EditorMaterialPlace(uint32_t type,uint32_t location,uint32_t sequence,uint32_t position){
 if(position!=POS_FACEUP_ATTACK&&position!=POS_FACEUP_DEFENSE&&position!=POS_FACEDOWN_DEFENSE)return false;
 if(location==LOCATION_EXTRA)return (type&(TYPES_EXTRA_DECK|TYPE_PENDULUM)) && (!(position&POS_FACEUP)||(type&TYPE_PENDULUM)) && ((type&TYPES_EXTRA_DECK)||(position&POS_FACEUP));
 if(location==LOCATION_HAND||location==LOCATION_DECK)return !(type&TYPES_EXTRA_DECK);
 if(location==LOCATION_MZONE)return sequence<7 && (sequence<5||(type&(TYPES_EXTRA_DECK|TYPE_PENDULUM))) && (!(type&TYPE_LINK)||position==POS_FACEUP_ATTACK);
 if(location==LOCATION_SZONE)return sequence<5;
 return location==LOCATION_GRAVE||location==LOCATION_REMOVED;
}
static int EditorSetSummon(duel* d,card* host,const editor_command& cmd){
 if(cmd.value>5 || host->current.location!=LOCATION_MZONE || !(host->summon_info&SUMMON_TYPE_SPECIAL))return EDITOR_BAD_DESTINATION;
 static const uint32_t kinds[]={0,TYPE_FUSION,TYPE_SYNCHRO,TYPE_XYZ,TYPE_LINK,TYPE_RITUAL};
 if(cmd.value && (!(host->get_type()&kinds[cmd.value]) || (cmd.value!=5 && ((host->summon_info>>16)&0xff)!=LOCATION_EXTRA)))return EDITOR_BAD_DESTINATION;
 host->summon_info=(host->summon_info&0xff0000U)|EditorSummonType(cmd.value);
 ClearTagged(host,5);host->editor_summon_materials.clear();host->material_cards.clear();return EDITOR_OK;
}
static int EditorAddSummonMaterial(duel* d,card* host,const editor_command& cmd){
 const uint32_t kind=EditorSummonKind(host);if(!kind||host->current.location!=LOCATION_MZONE||host->editor_summon_materials.size()>=255)return EDITOR_BAD_DESTINATION;
 card_data data{};read_card(cmd.code,&data);if(!data.code||data.code!=cmd.code||!(data.type&TYPE_MONSTER)||(data.type&TYPE_TOKEN))return EDITOR_UNKNOWN_CARD;
 if(!EditorMaterialPlace(data.type,cmd.location,cmd.sequence,cmd.position)||cmd.attack>1)return EDITOR_BAD_DESTINATION;
 const uint32_t destination=cmd.value,controller=cmd.attack,sequence=cmd.defense,position=cmd.level;
 if(kind==3){if(destination!=LOCATION_OVERLAY||host->xyz_materials.size()>=255)return EDITOR_BAD_DESTINATION;}
 else if(destination==LOCATION_OVERLAY||!EditorMaterialPlace(data.type,destination,sequence,position))return EDITOR_BAD_DESTINATION;
 if((destination==LOCATION_HAND||destination==LOCATION_DECK||destination==LOCATION_GRAVE||destination==LOCATION_EXTRA||destination==LOCATION_REMOVED)&&controller!=cmd.owner)return EDITOR_BAD_DESTINATION;
 uint64_t id=uint64_t(cmd.states)|(uint64_t(cmd.restrictions)<<32);card* material=nullptr;
 if(id){for(auto* c:d->cards)if(c->cardid==id){material=c;break;}if(!material||material==host||material->data.code!=cmd.code||!material->current.location||material->owner!=cmd.owner||material->equiping_target||!material->equiping_cards.empty()||!material->xyz_materials.empty()||(material->overlay_target&&material->overlay_target!=host))return EDITOR_BAD_DESTINATION;
  bool history=material->current.reason_card==host;
  if(!history&&(material->current.controler!=cmd.player||material->current.location!=cmd.location||material->current.sequence!=cmd.sequence))return EDITOR_STALE;
 }
 for(const auto& old:host->editor_summon_materials)if(id&&old.card_id==id)return EDITOR_BAD_COMMAND;
 if(destination&LOCATION_ONFIELD){auto* occupied=d->game_field->get_field_card(controller,destination,sequence);if(occupied&&occupied!=material)return EDITOR_OCCUPIED;}
 else if(destination!=LOCATION_OVERLAY&&query_field_count(reinterpret_cast<intptr_t>(d),controller,destination)>=254&&(!material||material->current.location!=destination))return EDITOR_BAD_DESTINATION;
 auto* f=d->game_field;uint8_t shuffleHand[2]={f->core.shuffle_hand_check[0],f->core.shuffle_hand_check[1]},shuffleDeck[2]={f->core.shuffle_deck_check[0],f->core.shuffle_deck_check[1]};
 if(!material){material=d->new_card(cmd.code);material->owner=cmd.owner;}
 else {material->enable_field_effect(false);if(material->overlay_target)material->overlay_target->xyz_remove(material);else d->game_field->remove_card(material);}
 material->sendto_param.position=position;
 if(destination==LOCATION_OVERLAY)host->xyz_add(material);else{f->add_card(controller,material,destination,sequence);material->current.position=position;material->enable_field_effect(!(destination&LOCATION_ONFIELD)||(position&POS_FACEUP));}
 for(int p=0;p<2;p++){f->core.shuffle_hand_check[p]=shuffleHand[p];f->core.shuffle_deck_check[p]=shuffleDeck[p];}
 material->previous.code=material->get_code();material->previous.type=data.type;material->previous.level=data.level;material->previous.attribute=data.attribute;material->previous.race=data.race;material->previous.attack=data.attack;material->previous.defense=data.defense;material->previous.controler=cmd.player;material->previous.location=cmd.location;material->previous.sequence=cmd.sequence;material->previous.position=cmd.position;
 static const uint32_t reasons[]={0,REASON_FUSION,REASON_SYNCHRO,REASON_XYZ,REASON_LINK,REASON_RITUAL};material->current.reason=REASON_MATERIAL|reasons[kind];material->current.reason_player=host->current.controler;material->current.reason_card=host;
 host->material_cards.insert(material);
 host->editor_summon_materials.push_back({host->cardid,material->cardid,cmd.code,cmd.owner,cmd.player,cmd.location,cmd.sequence,cmd.position,controller,destination,uint32_t(material->current.sequence),position});return EDITOR_OK;
}
extern "C" uint32_t editor_summon_kind(intptr_t handle,uint64_t id){if(!handle)return 0;for(auto* c:reinterpret_cast<duel*>(handle)->cards)if(c->cardid==id)return EditorSummonKind(c);return 0;}
extern "C" uint32_t editor_list_summon_materials(intptr_t handle,uint64_t id,editor_summon_material_info* output,uint32_t capacity){if(!handle)return 0;for(auto* c:reinterpret_cast<duel*>(handle)->cards)if(c->cardid==id){uint32_t n=0;for(const auto& item:c->editor_summon_materials){if(output&&n<capacity)output[n]=item;n++;}if(!n&&EditorSummonKind(c)){std::vector<card*> materials(c->material_cards.begin(),c->material_cards.end());std::sort(materials.begin(),materials.end(),[](card* a,card* b){return a->cardid<b->cardid;});for(auto* m:materials){if(output&&n<capacity)output[n]={c->cardid,m->cardid,m->data.code,m->owner,m->previous.controler,m->previous.location,m->previous.sequence,m->previous.position,m->current.location==LOCATION_OVERLAY?uint32_t(c->current.controler):uint32_t(m->current.controler),m->current.location,m->current.sequence,m->current.position};n++;}}return n;}return 0;}
static int EditorInitializeSummonMaterials(duel* d,card* host){
 if(!EditorSummonKind(host))return EDITOR_BAD_DESTINATION;
 auto* f=d->game_field;std::unordered_set<effect*> oldEffects=d->effects;
 std::list<processor_unit> suspended,suspendedSubunits;suspended.splice(suspended.end(),f->core.units);suspendedSubunits.splice(suspendedSubunits.end(),f->core.subunits);f->adjust_instant();
 host->set_material(&host->material_cards);
 static const uint32_t reasons[]={0,REASON_FUSION,REASON_SYNCHRO,REASON_XYZ,REASON_LINK,REASON_RITUAL};uint32_t reason=reasons[EditorSummonKind(host)];
 for(auto* material:host->material_cards)f->raise_single_event(material,nullptr,EVENT_BE_MATERIAL,nullptr,reason,host->summon_player,host->summon_player,0);
 f->raise_event(host->material_cards,EVENT_BE_MATERIAL,nullptr,reason,host->summon_player,host->summon_player,0);f->process_single_event();f->process_instant_event();
 auto drain=[&](){for(int i=0;i<10000&&(!f->core.units.empty()||!f->core.subunits.empty());i++){uint32_t result=f->process();if((result&PROCESSOR_FLAG)==PROCESSOR_WAITING)return false;}return f->core.units.empty()&&f->core.subunits.empty();};
 bool valid=drain();if(valid){f->raise_single_event(host,nullptr,EVENT_SPSUMMON_SUCCESS,nullptr,0,host->summon_player,host->summon_player,0);f->process_single_event();valid=drain();}
 f->core.units.splice(f->core.units.end(),suspended);f->core.subunits.splice(f->core.subunits.end(),suspendedSubunits);
 if(!valid)return EDITOR_UNSAFE;
 for(auto* e:d->effects)if(!oldEffects.count(e)&&e->handler==host)e->editor_kind=5;
 return EDITOR_OK;
}
