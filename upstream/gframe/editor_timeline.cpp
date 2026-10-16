#include "editor_timeline.h"
#include "editor_history.h"
#include "../ocgcore/duel.h"
#include "../ocgcore/field.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include "game.h"
#include "bufferio.h"
namespace ygo {
uint64_t EditorTimeline::Hash(intptr_t h) const{auto s=EditorHistory::Snapshot(h,header.value1>=6);uint64_t hash=1469598103934665603ULL;for(unsigned char c:s){hash^=c;hash*=1099511628211ULL;}return hash;}
void EditorTimeline::Reset(const uint32_t* seeds,const EditorSetup& initial,uint32_t opts,const Replay& replay){std::copy(seeds,seeds+SEED_COUNT,seed.begin());setup=initial;if(setup.hand1==0xffffffffU)setup.hand1=setup.hand;options=opts;header=replay.pheader;initialReplay=replay.CaptureRecording();journal.clear();logs.clear();points.clear();cursor=0;}
void EditorTimeline::InitialPoint(intptr_t h){Point p;p.hash=Hash(h);points.push_back(std::move(p));cursor=0;}
void EditorTimeline::Branch(){if(!points.empty() && CanForward()){journal.resize(points[cursor].index);points.resize(cursor+1);logs.erase(std::remove_if(logs.begin(),logs.end(),[&](const LogRow& r){return r.point>cursor+1;}),logs.end());}}
void EditorTimeline::Processed(intptr_t h,uint32_t result,bool checkpoint){Branch();Entry entry;entry.kind=0;entry.result=result;journal.push_back(entry);if(checkpoint){Point point;point.index=journal.size();point.hash=Hash(h);point.waiting=(result&PROCESSOR_FLAG)==PROCESSOR_WAITING;points.push_back(std::move(point));cursor=points.size()-1;}}
void EditorTimeline::Prompt(const uint8_t* data,size_t length){if(!points.empty() && points[cursor].index==journal.size())points[cursor].prompt.assign(data,data+length);}
void EditorTimeline::Response(const uint8_t* data,size_t length){Branch();Entry entry;entry.kind=1;entry.length=length;std::copy(data,data+length,entry.response.begin());journal.push_back(entry);}
void EditorTimeline::Edit(const editor_command& cmd){Branch();Entry entry;entry.kind=2;entry.command=cmd;journal.push_back(entry);}
bool EditorTimeline::Restore(intptr_t& h,Replay& replay,size_t target){
 if(target>=points.size())return false;
 auto restored=create_duel_v2(seed.data());editor_enable(restored,header.value1>=6?3:header.value1>=5?2:1);bool valid=setup.Initialize(restored);
 std::vector<uint8_t> buffer(SIZE_MESSAGE_BUFFER);if(valid){get_message(restored,buffer.data());start_duel(restored,options);}
 const auto& point=points[target];std::vector<uint8_t> replayBytes=initialReplay;
 uint32_t processPoint=0;size_t groupEnd=0;bool inGroup=false;
 for(size_t i=0;valid && i<point.index;++i){const auto& e=journal[i];if(e.kind==0){uint32_t result=process(restored);++processPoint;valid=result==e.result;if(result&PROCESSOR_BUFFER_LEN)get_message(restored,buffer.data());}
  else if(e.kind==1){set_responseb(restored,const_cast<uint8_t*>(e.response.data()));replayBytes.push_back(uint8_t(e.length));replayBytes.insert(replayBytes.end(),e.response.begin(),e.response.begin()+e.length);}
  else {if(e.command.opcode==11){uint32_t count=e.command.value;if(header.value1<6 || !count || count>512 || i+count>=point.index){valid=false;break;}std::vector<editor_command> group;for(uint32_t j=0;j<count;++j){if(journal[i+j+1].kind!=2){valid=false;break;}group.push_back(journal[i+j+1].command);}if(valid)valid=editor_apply_group(restored,group.data(),count)==EDITOR_OK;groupEnd=i+count;inGroup=true;}else if(!inGroup || i>groupEnd)valid=editor_apply(restored,&e.command)==EDITOR_OK;replayBytes.push_back(0);if(header.value1>=5){const auto* p=reinterpret_cast<const uint8_t*>(&processPoint);replayBytes.insert(replayBytes.end(),p,p+4);}replayBytes.push_back(uint8_t(e.command.version));const auto* bytes=reinterpret_cast<const uint8_t*>(&e.command);size_t size=e.command.version==1?40:e.command.version==2?48:sizeof(editor_command);replayBytes.insert(replayBytes.end(),bytes,bytes+size);}
 }
 valid=valid && Hash(restored)==point.hash;
 if(!valid || !replay.ReplaceRecording(header,replayBytes)){end_duel(restored);return false;}
 end_duel(h);h=restored;cursor=target;return true;
}
namespace {
constexpr uint32_t MAGIC=0x53454759,FORMAT=2;
template<class T>bool Read(std::istream& f,T& v){return bool(f.read(reinterpret_cast<char*>(&v),sizeof v));}
template<class T>void Write(std::ostream& f,const T& v){f.write(reinterpret_cast<const char*>(&v),sizeof v);}
bool ReadBytes(std::istream& f,std::vector<uint8_t>& bytes,size_t cap){uint32_t size=0;if(!Read(f,size)||size>cap)return false;bytes.resize(size);return size==0 || bool(f.read(reinterpret_cast<char*>(bytes.data()),size));}
void WriteBytes(std::ostream& f,const std::vector<uint8_t>& bytes){Write(f,uint32_t(bytes.size()));if(!bytes.empty())f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
}
bool EditorTimeline::Save(const std::string& file,const std::string& name) const{
 if(points.empty() || name.size()>256)return false;
 std::ofstream f(file,std::ios::binary|std::ios::trunc);if(!f)return false;Write(f,MAGIC);Write(f,FORMAT);Write(f,uint32_t(sizeof(editor_command)));std::vector<uint8_t> label(name.begin(),name.end());WriteBytes(f,label);
 f.write(reinterpret_cast<const char*>(seed.data()),sizeof seed);Write(f,options);Write(f,header);Write(f,setup.hand);Write(f,setup.hand1);
 for(const auto& d:setup.decks)for(const auto* list:{&d.main,&d.extra}){Write(f,uint32_t(list->size()));if(!list->empty())f.write(reinterpret_cast<const char*>(list->data()),list->size()*4);}
 WriteBytes(f,initialReplay);Write(f,uint32_t(points[cursor].index));for(size_t i=0;i<points[cursor].index;++i)Write(f,journal[i]);
 Write(f,uint32_t(cursor+1));for(size_t i=0;i<=cursor;++i){const auto& p=points[i];Write(f,uint32_t(p.index));Write(f,p.hash);Write(f,uint32_t(p.waiting));WriteBytes(f,p.prompt);}uint32_t count=0;for(const auto& row:logs)if(row.point<=cursor+1)++count;Write(f,count);for(const auto& row:logs)if(row.point<=cursor+1){Write(f,row.point);Write(f,row.param);WriteBytes(f,std::vector<uint8_t>(row.text.begin(),row.text.end()));}return bool(f);
}
std::string EditorTimeline::ReadName(const std::string& file){std::ifstream f(file,std::ios::binary);uint32_t magic=0,format=0,size=0;std::vector<uint8_t> name;if(!Read(f,magic)||!Read(f,format)||!Read(f,size)||magic!=MAGIC||(format!=1 && format!=FORMAT)||size!=sizeof(editor_command)||!ReadBytes(f,name,256))return {};return std::string(name.begin(),name.end());}
bool EditorTimeline::Load(const std::string& file,intptr_t& h,Replay& r){
 std::ifstream f(file,std::ios::binary);uint32_t magic=0,format=0,size=0;std::vector<uint8_t> name;
 if(!Read(f,magic)||!Read(f,format)||!Read(f,size)||magic!=MAGIC||(format!=1 && format!=FORMAT)||size!=sizeof(editor_command)||!ReadBytes(f,name,256))return false;
 EditorTimeline loaded;if(!f.read(reinterpret_cast<char*>(loaded.seed.data()),sizeof loaded.seed)||!Read(f,loaded.options)||!Read(f,loaded.header)||!Read(f,loaded.setup.hand)||!Read(f,loaded.setup.hand1)||loaded.setup.hand>254||loaded.setup.hand1>254)return false;
 for(auto& d:loaded.setup.decks)for(auto* list:{&d.main,&d.extra}){uint32_t count=0;if(!Read(f,count)||count>254)return false;list->resize(count);if(count && !f.read(reinterpret_cast<char*>(list->data()),count*4))return false;}
 if(!ReadBytes(f,loaded.initialReplay,MAX_REPLAY_SIZE)||loaded.initialReplay.size()<100)return false;
 uint32_t count=0;if(!Read(f,count)||count>1000000)return false;loaded.journal.resize(count);for(auto& e:loaded.journal){if(!Read(f,e)||e.kind>2||e.length>SIZE_RETURN_VALUE)return false;if(e.kind==2 && (e.command.version<1||e.command.version>9))return false;}
 if(!Read(f,count)||!count||count>100000)return false;loaded.points.resize(count);size_t previous=0;
 for(auto& p:loaded.points){uint32_t index=0,waiting=0;if(!Read(f,index)||index>loaded.journal.size()||index<previous||!Read(f,p.hash)||!Read(f,waiting)||waiting>1||!ReadBytes(f,p.prompt,SIZE_MESSAGE_BUFFER))return false;p.index=index;p.waiting=waiting!=0;previous=index;if(p.waiting && p.prompt.empty())return false;}
 if(format>=2){if(!Read(f,count)||count>100000)return false;for(uint32_t i=0;i<count;++i){LogRow row{};std::vector<uint8_t> text;if(!Read(f,row.point)||!row.point||row.point>loaded.points.size()||!Read(f,row.param)||!ReadBytes(f,text,65536))return false;row.text.assign(text.begin(),text.end());loaded.logs.push_back(std::move(row));}}
 if(f.peek()!=std::char_traits<char>::eof())return false;
 if(!loaded.Restore(h,r,loaded.points.size()-1))return false;*this=std::move(loaded);return true;
}
}

namespace ygo {
intptr_t EditorTimeline::RebuildCurrent() const{auto h=intptr_t(0);Replay scratch;scratch.BeginRecord(true);auto copy=*this;if(!copy.Restore(h,scratch,cursor))return 0;return h;}
int EditorTimeline::EditGroup(intptr_t& h,std::vector<editor_command>& group){auto candidate=RebuildCurrent();if(!candidate)return EDITOR_UNSAFE;int result=editor_apply_group(candidate,group.data(),uint32_t(group.size()));if(result==EDITOR_OK){end_duel(h);h=candidate;editor_command marker{};marker.opcode=11;marker.value=uint32_t(group.size());Edit(marker);for(const auto& c:group)Edit(c);}else end_duel(candidate);return result;}
void EditorTimeline::Log(const wchar_t* text,int param,uint32_t point){if(!point&&!Number())return;char utf8[65536]{};BufferIO::EncodeUTF8(text,utf8);logs.push_back({point?point:uint32_t(Number()),utf8,param});}
void EditorTimeline::RestoreLogs() const{std::lock_guard<std::mutex> lock(mainGame->gMutex);mainGame->lstLog->clear();mainGame->logParam.clear();for(const auto& row:logs)if(row.point<=Number()){wchar_t text[65536]{};BufferIO::DecodeUTF8(row.text.c_str(),text);mainGame->lstLog->addItem(text);mainGame->logParam.push_back(row.param);}}
}
