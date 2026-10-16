#ifndef EDITOR_HISTORY_H
#define EDITOR_HISTORY_H
#include "../ocgcore/editor.h"
#include <array>
#include <vector>
#include <string>
#include "editor_setup.h"
namespace ygo {
class Replay;
class EditorHistory {
 struct Input {
  bool edit{false};editor_command command{};
  std::array<unsigned char,SIZE_RETURN_VALUE> response{};
  unsigned int length{0};size_t recordedBytes{0};
 };
 struct Checkpoint {size_t index;std::string state;};
 std::array<uint32_t,SEED_COUNT> seed{};
 std::string script;
 EditorSetup initialSetup;bool customSetup{false};
 uint32_t options{0};
 std::vector<Input> inputs;
 std::vector<Checkpoint> checkpoints;
 static bool PumpToInput(intptr_t handle);
 static std::string State(intptr_t handle,bool extended=false);
public:
 void Reset(const uint32_t* seeds,const std::string& scriptName,uint32_t duelOptions);
 void SetInitialSetup(const EditorSetup& setup){initialSetup=setup;customSetup=true;}
 void RecordResponse(const unsigned char* data,unsigned int length,size_t recordedBytes);
 void RecordEdit(const editor_command& command,size_t recordedBytes);
 void MarkCheckpoint(intptr_t handle);
 void DiscardRejectedResponse();
 static std::string Snapshot(intptr_t h,bool extended=false){return State(h,extended);}
 bool CanUndo() const;
 bool Undo(intptr_t& handle,Replay& replay);
};
}
#endif
