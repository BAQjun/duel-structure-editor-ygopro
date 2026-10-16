#ifndef DUEL_EDITOR_UI_H
#define DUEL_EDITOR_UI_H
#include <IEventReceiver.h>
#include <atomic>
#include <vector>
#include <utility>
#include <string>
#include "editor_setup.h"
#include "../ocgcore/editor.h"
namespace ygo {
class DuelEditor {
public:
 static std::atomic<bool> active;
 static void Initialize();
 static uint32_t CurrentView();
 static uint32_t TimingMode(uint32_t player);
 static void CaptureTiming();
 static void SetPromptPlayer(int player);
 static void ApplyViewSwitch();
 static void MaskPendingPrompt();
 static void Begin();
 static EditorSetup initialSetup;
 static bool OnEvent(const irr::SEvent& event);
 static void Available(bool enabled, uint32_t revision, bool canUndo=false);
 static void Cards(const std::vector<editor_card_info>& cards,uint32_t revision);
 static void Attachments(const std::vector<editor_material_info>& materials,const std::vector<editor_counter_info>& counters);
 static void SummonHistories(const std::vector<std::pair<uint64_t,uint32_t>>& kinds,const std::vector<editor_summon_material_info>& materials);
 static void CustomEffects(const std::vector<editor_effect_info>& effects);
 static bool OpenCard(uint32_t player,uint32_t location,uint32_t sequence);
 static void Completed(int result);
 static void Transport(bool paused,bool canBack,bool canForward,bool boundary,size_t point);
 static void SaveResult(const wchar_t* text);
 static void Layout();
 static void DrawTransport();
 static void Shutdown();
 static std::wstring PlayerName(uint32_t player);
 static void RefreshPlayerNames();
};
}
#endif
