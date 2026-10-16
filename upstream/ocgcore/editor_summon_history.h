#ifndef EDITOR_SUMMON_HISTORY_H
#define EDITOR_SUMMON_HISTORY_H
#include <cstdint>
struct editor_summon_material_info {
 uint64_t host_id,card_id;
 uint32_t code,owner,source_player,source_location,source_sequence,source_position,destination_player,destination_location,destination_sequence,destination_position;
};
#endif
