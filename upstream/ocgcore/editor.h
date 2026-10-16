#ifndef DUEL_EDITOR_API_H
#define DUEL_EDITOR_API_H
#include "ocgapi.h"
#include "editor_summon_history.h"
// v9 stores custom Link arrows in defense for Link forms and separates non-effect Extra/Ritual types from Normal.
// v6 adds grave-turn and Extra insertion semantics; v7 adds method bits 32/64/128 and source byte at value bits 8-15; v8 limits trap monsters to Main Monster Zones. All retain 88 bytes.
// Wire v4 keeps v3 size; forms 5/6 use monster_flags as custom type (form 6 bit 31 retains spell/trap identity) and self_summon as name code.
// Wire v1: 10 words; v2: 12 words; v3: 22 little-endian uint32_t words.
struct editor_command {
 uint32_t version{9};
 uint32_t opcode{0}; // 1: create, 2: LP, 3: delete by stable ID, 4: edit by stable ID, 5: reorder deck by stable ID, 6: constant card effect, 7: explicitly shuffle deck
 uint32_t revision{0};
 uint32_t code{0};
 uint32_t owner{0};
 uint32_t player{0};
 uint32_t location{0};
 uint32_t sequence{0}; // field index; deck top/bottom; list zones append
 uint32_t position{POS_FACEUP_ATTACK};
 uint32_t value{0}; // LP or card flags
 uint32_t target_low{0};
 uint32_t target_high{0};
 uint32_t form{0},race{0},attribute{0},attack{0},defense{0},level{4},monster_flags{1},self_summon{0},states{0},restrictions{0};
};
static_assert(sizeof(editor_command)==88,"Editor wire layout changed");
struct editor_card_info {
 uint64_t id{0};uint32_t code{0},type{0},owner{0},player{0},location{0},sequence{0},position{0},flags{0},pzone{0},form{0},race{0},attribute{0},attack{0},defense{0},level{0},monster_flags{0},self_summon{0},states{0},restrictions{0},actual_status{0},effective_type{0},effective_code{0},summon_source{0},summon_method{0},link_marker{0};
};
struct editor_material_info {uint64_t host_id,card_id;uint32_t code,owner;};
struct editor_counter_info {uint64_t card_id;uint32_t type,count;};
struct editor_effect_info {uint64_t card_id;uint32_t code;int32_t value;};
enum editor_creation_flags : uint32_t { EDITOR_PROC_COMPLETE=1, EDITOR_SUMMON_NORMAL=2, EDITOR_SUMMON_SPECIAL=4, EDITOR_SUMMON_FLIP=8, EDITOR_SENT_GRAVE_TURN=16, EDITOR_SPECIAL_ORIGIN=32, EDITOR_NORMAL_ORIGIN=64, EDITOR_FLIP_ORIGIN=128 };
// Matches card::is_summonable_card's intrinsic type test; tribute availability
// and the current turn's used summon count do not describe historical placement.
inline bool editor_allows_normal_summon(uint32_t type) {
 return (type&TYPE_MONSTER) && !(type&(TYPES_EXTRA_DECK|TYPE_RITUAL|TYPE_SPSUMMON|TYPE_TOKEN));
}
enum editor_result { EDITOR_OK=0, EDITOR_DISABLED=1, EDITOR_UNSAFE=2,
 EDITOR_STALE=3, EDITOR_BAD_COMMAND=4, EDITOR_UNKNOWN_CARD=5,
 EDITOR_BAD_DESTINATION=6, EDITOR_OCCUPIED=7 };
extern "C" {
OCGCORE_API uint32_t editor_summon_kind(intptr_t handle,uint64_t id);
OCGCORE_API uint32_t editor_list_summon_materials(intptr_t handle,uint64_t id,editor_summon_material_info* output,uint32_t capacity);
OCGCORE_API int editor_native_trap_profile(uint32_t code,editor_card_info* output);
OCGCORE_API void editor_enable(intptr_t pduel, int enabled);
OCGCORE_API int editor_can_edit(intptr_t pduel);
OCGCORE_API uint32_t editor_revision(intptr_t pduel);
OCGCORE_API uint32_t editor_list_cards(intptr_t pduel,editor_card_info* output,uint32_t capacity);
OCGCORE_API int editor_setup_decks(intptr_t handle, const uint32_t* main0,uint32_t count0,const uint32_t* extra0,uint32_t extraCount0,const uint32_t* main1,uint32_t count1,const uint32_t* extra1,uint32_t extraCount1,uint32_t hand,uint32_t hand1=0xffffffffU);
OCGCORE_API uint32_t editor_list_materials(intptr_t handle,editor_material_info* output,uint32_t capacity);
OCGCORE_API uint32_t editor_list_counters(intptr_t handle,editor_counter_info* output,uint32_t capacity);
OCGCORE_API uint32_t editor_list_custom_effects(intptr_t handle,editor_effect_info* output,uint32_t capacity);
OCGCORE_API int editor_apply(intptr_t pduel, const editor_command* command);
OCGCORE_API int editor_apply_group(intptr_t pduel,editor_command* commands,uint32_t count);
}
#endif
