#ifndef EDITOR_TIMELINE_H
#define EDITOR_TIMELINE_H
#include "editor_setup.h"
#include "replay.h"
#include <array>
#include <string>
namespace ygo {
class EditorTimeline {
 struct Entry {uint32_t kind{0},result{0},length{0};editor_command command{};std::array<uint8_t,SIZE_RETURN_VALUE> response{};};
 struct Point {size_t index{0};uint64_t hash{0};bool waiting{false};std::vector<uint8_t> prompt;};
 std::array<uint32_t,SEED_COUNT> seed{};EditorSetup setup;uint32_t options{0};ExtendedReplayHeader header{};std::vector<uint8_t> initialReplay;
 struct LogRow{uint32_t point;std::string text;int32_t param;};std::vector<LogRow> logs;
 std::vector<Entry> journal;std::vector<Point> points;size_t cursor{0};
 uint64_t Hash(intptr_t h) const;
 void Branch();
 bool Restore(intptr_t& h,Replay& replay,size_t target);
public:
 void Reset(const uint32_t* seeds,const EditorSetup& initial,uint32_t opts,const Replay& replay);
 void InitialPoint(intptr_t h);
 void Processed(intptr_t h,uint32_t result,bool checkpoint);
 void Prompt(const uint8_t* data,size_t length);
 void Response(const uint8_t* data,size_t length);
 void Edit(const editor_command& cmd);
 int EditGroup(intptr_t& h,std::vector<editor_command>& group);
 void Log(const wchar_t* text,int param,uint32_t point=0);
 void RestoreLogs() const;
 intptr_t RebuildCurrent() const;
 bool CanBack() const{return !points.empty() && cursor>0;}
 bool CanForward() const{return !points.empty() && cursor+1<points.size();}
 bool Back(intptr_t& h,Replay& r){return CanBack() && Restore(h,r,cursor-1);}
 bool Forward(intptr_t& h,Replay& r){return CanForward() && Restore(h,r,cursor+1);}
 bool Waiting() const{return !points.empty() && points[cursor].waiting;}
 const std::vector<uint8_t>& CurrentPrompt() const{return points[cursor].prompt;}
 size_t Number() const{return points.empty()?0:cursor+1;}
 uint32_t ProcessCount() const {uint32_t count=0;size_t limit=points.empty()?journal.size():points[cursor].index;for(size_t i=0;i<limit;++i)if(journal[i].kind==0)++count;return count;}
 bool Save(const std::string& file,const std::string& name) const;
 bool Load(const std::string& file,intptr_t& h,Replay& r);
 static std::string ReadName(const std::string& file);
};
}
#endif
