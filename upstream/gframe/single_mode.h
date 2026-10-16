#ifndef SINGLE_MODE_H
#define SINGLE_MODE_H

#include <cstdint>
#include <vector>
#include "replay.h"
#include "../ocgcore/editor.h"
#include <atomic>

namespace ygo {

class SingleMode {
private:
	static intptr_t pduel;
	static std::atomic<bool> is_closing;
	static std::atomic<bool> is_continuing;
	static void ReloadLocation(int player, int location, int flag, std::vector<unsigned char>& queryBuffer);

public:
	static bool StartPlay(const std::string& startupSave={});
	static bool QueueEditorCommand(const editor_command& command);
 static bool QueueEditorGroup(const std::vector<editor_command>& commands);
 static void RecordEditorLog(const wchar_t* text,int param);
	static bool QueueUndo();
 static std::atomic<bool> paused;
 static void TogglePause();
 static void Step();
 static void QueueViewSwitch();
 static bool QueueSave(const std::string& file,const std::string& name);
 static bool QueueLoad(const std::string& file);
 static void Gate();
 static bool AtBoundary();
 static bool ServiceActions(bool waiting);
 static void RestoreView();
	static void RefreshEditorState();
	static void AnalyzeAndWait(unsigned char* msg, unsigned int len);
	static void StopPlay(bool is_exiting = false);
	static void SetResponse(unsigned char* resp, unsigned int len);
	static void SinglePlayThread();
	static bool SinglePlayAnalyze(unsigned char* msg, unsigned int len);
	
	static void SinglePlayRefresh(int flag = 0xf81fff);
	static void SingleRefreshLocation(int player, int location, int flag);
	static void SinglePlayRefreshHand(int player, int flag = 0x781fff);
	static void SinglePlayRefreshGrave(int player, int flag = 0x181fff);
	static void SinglePlayRefreshDeck(int player, int flag = 0x181fff);
	static void SinglePlayRefreshExtra(int player, int flag = 0x181fff);
	static void SinglePlayRefreshSingle(int player, int location, int sequence, int flag = 0xf81fff);
	static void SinglePlayReload();

	static uint32_t MessageHandler(intptr_t fduel, uint32_t type);

protected:
	static Replay last_replay;
	static size_t last_replay_response_size;
};

}

#endif //SINGLE_MODE_H
