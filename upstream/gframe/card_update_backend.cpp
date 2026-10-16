#include "card_update.h"
#include <windows.h>
#include <winhttp.h>
#include <sqlite3.h>
#include <zlib.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <mutex>
namespace ygo {
namespace {
using Bytes=std::vector<unsigned char>;
std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&w[0],n);return w;}
std::string Narrow(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string b(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),&b[0],n,nullptr,nullptr);return b;}
bool Exists(const std::string& p){return GetFileAttributesW(Wide(p).c_str())!=INVALID_FILE_ATTRIBUTES;}
void Directory(const std::string& p){auto at=p.find_last_of("/\\");if(at!=std::string::npos && at>0 && !Exists(p.substr(0,at)))Directory(p.substr(0,at));if(!Exists(p) && !CreateDirectoryW(Wide(p).c_str(),nullptr))throw std::runtime_error("无法创建更新目录");}
Bytes Read(const std::string& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("无法读取更新文件："+p);f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0 || n>256*1024*1024)throw std::runtime_error("更新文件过大");f.seekg(0);Bytes b(size_t(n),0);if(n && !f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("更新文件不完整");return b;}
void Write(const std::string& p,const Bytes& b){Directory(p.substr(0,p.find_last_of('/')));std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(b.data()),b.size());if(!f)throw std::runtime_error("无法写入更新文件");}
void Copy(const std::string& a,const std::string& b){Directory(b.substr(0,b.find_last_of('/')));if(!CopyFileW(Wide(a).c_str(),Wide(b).c_str(),FALSE))throw std::runtime_error("无法复制更新文件");}
void CopyTree(const std::string& a,const std::string& b){if(!Exists(a))return;Directory(b);WIN32_FIND_DATAW info{};HANDLE h=FindFirstFileW(Wide(a+"/*").c_str(),&info);if(h==INVALID_HANDLE_VALUE)return;try{do{std::wstring name=info.cFileName;if(name==L"." || name==L"..")continue;if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("更新目录不接受符号链接");auto s=Narrow(name);if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)CopyTree(a+"/"+s,b+"/"+s);else Copy(a+"/"+s,b+"/"+s);}while(FindNextFileW(h,&info));}catch(...){FindClose(h);throw;}FindClose(h);}
void Move(const std::string& a,const std::string& b){if(!MoveFileExW(Wide(a).c_str(),Wide(b).c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法切换卡片版本；请检查目录写入权限");}
std::string Stamp(){SYSTEMTIME t{};GetSystemTime(&t);char s[80]{};std::snprintf(s,sizeof s,"%04u%02u%02u-%02u%02u%02u-%llu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,(unsigned long long)GetTickCount64());return s;}
Bytes Fetch(const std::string& url,const std::function<void(const std::string&)>& progress,std::atomic<bool>& cancel){return CardDownload::Get(url,[&](const CardDownloadProgress& p){progress(CardDownload::Describe(p));},cancel);}
uint16_t U16(const Bytes& b,size_t i){if(i+2>b.size())throw std::runtime_error("ZIP 文件不完整");return uint16_t(b[i])|uint16_t(b[i+1])<<8;}
uint32_t U32(const Bytes& b,size_t i){return uint32_t(U16(b,i))|uint32_t(U16(b,i+2))<<16;}
bool SafeName(const std::string& s){if(s.empty() || s.front()=='/' || s.find('\\')!=std::string::npos || s.find(':')!=std::string::npos || s.find('\0')!=std::string::npos)return false;std::istringstream in(s);std::string part;while(std::getline(in,part,'/'))if(part==".." || part==".")return false;return true;}
std::map<std::string,Bytes> Zip(const Bytes& zip,bool scripts){
 size_t end=zip.size();bool found=false;for(size_t n=0;n<std::min(size_t(65557),zip.size());++n){size_t pos=zip.size()-n-1;if(pos+22<=zip.size() && U32(zip,pos)==0x06054b50){end=pos;found=true;break;}}
 if(!found || U16(zip,end+4) || U16(zip,end+6))throw std::runtime_error("不支持的更新压缩包");size_t count=U16(zip,end+10),pos=U32(zip,end+16),total=0;std::map<std::string,Bytes> files;
 for(size_t i=0;i<count;++i){if(U32(zip,pos)!=0x02014b50)throw std::runtime_error("更新包目录损坏");auto flags=U16(zip,pos+8),method=U16(zip,pos+10);uint32_t crc=U32(zip,pos+16),packed=U32(zip,pos+20),size=U32(zip,pos+24),offset=U32(zip,pos+42);size_t names=U16(zip,pos+28),extra=U16(zip,pos+30),comment=U16(zip,pos+32);if(pos+46+names+extra+comment>zip.size())throw std::runtime_error("更新包目录越界");std::string name(reinterpret_cast<const char*>(zip.data()+pos+46),names);pos+=46+names+extra+comment;
  if(!SafeName(name))throw std::runtime_error("更新包包含不安全的文件路径");
  if(scripts){auto slash=name.find('/');if(slash==std::string::npos)continue;name=name.substr(slash+1);}
  if(name.empty() || name.back()=='/')continue;
  if(scripts && (name.size()<4 || name.substr(name.size()-4)!=".lua"))continue;
  if((flags&1) || size>32*1024*1024 || total+size>256*1024*1024 || (method!=0 && method!=8) || U32(zip,offset)!=0x04034b50)throw std::runtime_error("不支持或过大的更新包成员");size_t start=offset+30+U16(zip,offset+26)+U16(zip,offset+28);if(start+packed>zip.size())throw std::runtime_error("更新包内容越界");Bytes bytes(size,0);
  if(method==0){if(size!=packed)throw std::runtime_error("ZIP 长度错误");std::copy(zip.begin()+start,zip.begin()+start+size,bytes.begin());}
  else{z_stream stream{};stream.next_in=const_cast<Bytef*>(zip.data()+start);stream.avail_in=packed;stream.next_out=bytes.data();stream.avail_out=size;if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)throw std::runtime_error("ZIP 解压失败");int result=inflate(&stream,Z_FINISH);bool ok=result==Z_STREAM_END && stream.total_out==size;inflateEnd(&stream);if(!ok)throw std::runtime_error("ZIP 内容不完整");}
  if(crc32(0,bytes.data(),size)!=crc)throw std::runtime_error("ZIP 校验失败");if(!files.emplace(name,std::move(bytes)).second)throw std::runtime_error("更新包成员重复");total+=size;
 }
 return files;
}
struct DB {sqlite3* db{};DB(const std::string& p,bool writable=false){if(sqlite3_open_v2(p.c_str(),&db,writable?SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE:SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error("无法打开卡片数据库");}}~DB(){sqlite3_close(db);}void Exec(const std::string& sql){char* error=nullptr;if(sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){std::string text=error?error:"数据库操作失败";sqlite3_free(error);throw std::runtime_error(text);}}};
struct Row {std::string name,fingerprint;uint32_t alias{},type{};};using Rows=std::map<uint32_t,Row>;
const char* DataColumns="id,ot,alias,setcode,type,atk,def,level,race,attribute,category";
const char* TextColumns="id,name,desc,str1,str2,str3,str4,str5,str6,str7,str8,str9,str10,str11,str12,str13,str14,str15,str16";
Rows Cards(const std::string& path){DB db(path);sqlite3_stmt* query=nullptr;if(sqlite3_prepare_v2(db.db,"PRAGMA quick_check",-1,&query,nullptr)!=SQLITE_OK)throw std::runtime_error("数据库校验失败");bool ok=sqlite3_step(query)==SQLITE_ROW && std::string(reinterpret_cast<const char*>(sqlite3_column_text(query,0)))=="ok";sqlite3_finalize(query);if(!ok)throw std::runtime_error("卡片数据库损坏");
 const char* sql="SELECT datas.id,datas.ot,datas.alias,datas.setcode,datas.type,datas.atk,datas.def,datas.level,datas.race,datas.attribute,datas.category,texts.name,texts.desc,texts.str1,texts.str2,texts.str3,texts.str4,texts.str5,texts.str6,texts.str7,texts.str8,texts.str9,texts.str10,texts.str11,texts.str12,texts.str13,texts.str14,texts.str15,texts.str16 FROM datas JOIN texts ON datas.id=texts.id";
 if(sqlite3_prepare_v2(db.db,sql,-1,&query,nullptr)!=SQLITE_OK)throw std::runtime_error("卡片数据库格式不兼容");Rows rows;int step;
 while((step=sqlite3_step(query))==SQLITE_ROW){auto id=sqlite3_column_int64(query,0);if(id<=0 || id>0x0fffffff){sqlite3_finalize(query);throw std::runtime_error("卡片密码超出原版核心范围");}Row row;row.alias=uint32_t(sqlite3_column_int64(query,2));row.type=uint32_t(sqlite3_column_int64(query,4));for(int col=0;col<29;++col){auto* value=sqlite3_column_text(query,col);int length=sqlite3_column_bytes(query,col);row.fingerprint+=std::to_string(length)+":";if(value)row.fingerprint.append(reinterpret_cast<const char*>(value),length);}auto* name=sqlite3_column_text(query,11);if(name)row.name=reinterpret_cast<const char*>(name);rows.emplace(uint32_t(id),std::move(row));}
 sqlite3_finalize(query);if(step!=SQLITE_DONE || rows.empty())throw std::runtime_error("卡片数据库没有有效卡片");return rows;
}
Rows PackCards(const std::string& file,const std::string& dir){auto members=Zip(Read(file),false);Rows cards;size_t databases=0;for(const auto& member:members)if(member.first.size()>=4 && member.first.substr(member.first.size()-4)==".cdb"){auto path=dir+"/pack-"+std::to_string(databases++)+".cdb";Write(path,member.second);auto rows=Cards(path);for(auto& row:rows)cards[row.first]=std::move(row.second);}if(cards.empty())throw std::runtime_error("超先行包没有有效数据库");return cards;}
std::string Quoted(const std::string& p){std::string r="'";for(char c:p){r+=c;if(c=='\'')r+='\'';}return r+"'";}
void Merge(const std::string& from,const std::string& to){DB target(to,true);target.Exec("ATTACH DATABASE "+Quoted(from)+" AS source;BEGIN IMMEDIATE;");try{for(const auto& table:{std::pair<const char*,const char*>{"datas",DataColumns},{"texts",TextColumns}})target.Exec(std::string("INSERT OR REPLACE INTO ")+table.first+" ("+table.second+") SELECT "+table.second+" FROM source."+table.first+";");target.Exec("COMMIT;");}catch(...){target.Exec("ROLLBACK;");throw;}}
void Compare(CardUpdateReport& report,const Rows& local,const Rows& source,bool pre){for(const auto& card:source){if(pre){auto prior=std::find_if(report.cards.begin(),report.cards.end(),[&card](const CardUpdateDifference& d){return d.code==card.first;});if(prior!=report.cards.end()){if(prior->modified)--report.changed;else --report.missing;report.cards.erase(prior);}}auto old=local.find(card.first);bool added=old==local.end(),changed=!added && old->second.fingerprint!=card.second.fingerprint;if(added || changed){report.missing+=added;report.changed+=changed;report.cards.push_back({card.first,card.second.name,changed,pre});}}}
std::string Managed(const std::string& root){return root+"/.card-updates";}
}
void CardUpdatePlan::Check(bool official,bool prerelease,const std::function<void(const std::string&)>& progress,std::atomic<bool>& cancel,const CardDownloadCallback& details){
 if(!official && !prerelease)throw std::runtime_error("请至少选择一个更新源");cache=Managed(root)+"/cache/"+Stamp();Directory(cache);
 struct Job {std::string url,file,label;};std::vector<Job> jobs;
 if(official){jobs.push_back({"https://raw.githubusercontent.com/mycard/ygopro-database/master/locales/zh-CN/cards.cdb","regular.cdb","正式卡片数据库"});jobs.push_back({"https://codeload.github.com/Fluorohydride/ygopro-scripts/zip/refs/heads/master","scripts.zip","正式卡片效果脚本"});}
 if(prerelease){progress("正在确认最新超先行版本…");auto tag=Fetch("https://cdntx.moecube.com/ygopro-super-pre/data/latest-tag.txt",progress,cancel);packUrl=std::string(tag.begin(),tag.end());while(!packUrl.empty() && (packUrl.back()=='\r'||packUrl.back()=='\n'||packUrl.back()==' '))packUrl.pop_back();if(packUrl.find("https://cdntx.moecube.com/ygopro-super-pre/archive/")!=0 || packUrl.size()<4 || packUrl.substr(packUrl.size()-4)!=".ypk")throw std::runtime_error("官方超先行版本地址异常");jobs.push_back({packUrl,"superpre.ypk","超先行卡片／卡图包"});}
 progress("正在并行检查／下载所选来源…");std::vector<CardDownloadProgress> states(jobs.size());std::mutex progressMutex;std::exception_ptr failure;std::vector<std::thread> workers;
 for(size_t i=0;i<jobs.size();++i)workers.emplace_back([&,i]{try{auto bytes=CardDownload::Cached(jobs[i].url,Managed(root)+"/downloads",[&,i](const CardDownloadProgress& state){std::lock_guard<std::mutex> lock(progressMutex);states[i]=state;CardDownloadProgress total;total.file=jobs[i].label;total.cached=true;bool known=true;for(const auto& p:states){total.downloaded+=p.downloaded;total.total+=p.total;total.resumed+=p.resumed;total.bytesPerSecond+=p.bytesPerSecond;total.connections+=p.connections;total.cached&=p.cached;known&=p.total!=0;}if(!known)total.total=0;progress(CardDownload::Describe(total));if(details)details(total);},cancel);Write(cache+"/"+jobs[i].file,bytes);}catch(...){std::lock_guard<std::mutex> lock(progressMutex);if(!failure)failure=std::current_exception();cancel=true;}});
 for(auto& worker:workers)worker.join();if(failure)std::rethrow_exception(failure);if(cancel)throw std::runtime_error("已取消检查，完成分段保留");progress("正在核对卡片、文本和脚本差异…");Prepare(cache,official,prerelease);
}
void CardUpdatePlan::Prepare(const std::string& fixtures,bool official,bool prerelease){
 if(!official && !prerelease)throw std::runtime_error("请选择更新源");cache=fixtures;regular=official;pre=prerelease;report={};auto active=ActivePath(root);Rows local=Cards(Exists(active+"/cards.cdb")?active+"/cards.cdb":root+"/cards.cdb");if(Exists(active+"/superpre.ypk")){auto extra=PackCards(active+"/superpre.ypk",cache+"/local");for(auto& row:extra)local[row.first]=std::move(row.second);}report.local=local.size();
 if(regular){auto rows=Cards(cache+"/regular.cdb");report.regular=rows.size();Compare(report,local,rows,false);auto scripts=Zip(Read(cache+"/scripts.zip"),true);if(scripts.empty())throw std::runtime_error("正式卡片脚本包为空");for(const auto& member:scripts){if(member.first.substr(member.first.size()-4)!=".lua")continue;auto installed=active+"/script/"+member.first;if(!Exists(installed))installed=root+"/script/"+member.first;if(!Exists(installed) || Read(installed)!=member.second)++report.scriptChanges;}
  for(const auto& card:rows){if(!(card.second.type&0x20) || (card.second.type&0x4000))continue;std::string file="c"+std::to_string(card.first)+".lua",alias="c"+std::to_string(card.second.alias)+".lua";if(!scripts.count(file) && !(card.second.alias && scripts.count(alias)) && !Exists(root+"/script/"+file))throw std::runtime_error("正式卡片数据库与效果脚本未对齐，缺少卡号 "+std::to_string(card.first)+" 的脚本");}
 }
 if(pre){std::map<std::string,Bytes> formalScripts;if(regular)formalScripts=Zip(Read(cache+"/scripts.zip"),true);auto rows=PackCards(cache+"/superpre.ypk",cache+"/remote");report.pre=rows.size();Compare(report,local,rows,true);auto members=Zip(Read(cache+"/superpre.ypk"),false);for(const auto& card:rows)if((card.second.type&0x20) && !(card.second.type&0x4000) && !members.count("script/c"+std::to_string(card.first)+".lua") && !(card.second.alias && members.count("script/c"+std::to_string(card.second.alias)+".lua")) && !formalScripts.count("c"+std::to_string(card.first)+".lua") && !(card.second.alias && (formalScripts.count("c"+std::to_string(card.second.alias)+".lua") || Exists(active+"/script/c"+std::to_string(card.second.alias)+".lua") || Exists(root+"/script/c"+std::to_string(card.second.alias)+".lua"))) && !Exists(active+"/script/c"+std::to_string(card.first)+".lua") && !Exists(root+"/script/c"+std::to_string(card.first)+".lua"))throw std::runtime_error("超先行卡片缺少配套脚本："+std::to_string(card.first));
  for(const auto& member:members)if(member.first.substr(member.first.size()-4)==".lua"){std::string relative=member.first.find("script/")==0?member.first.substr(7):member.first;auto installed=active+"/pre-script/"+relative;if(!Exists(installed)||Read(installed)!=member.second)++report.scriptChanges;}
 }
}
namespace {
std::string Identifier(const std::string& base,const std::string& path){if(path=="base" || path==base+"/base")return "base";if(path=="legacy" || path==base+"/active")return "legacy";auto prefix=base+"/versions/";if(path.find(prefix)==0)return path.substr(prefix.size());if(SafeName(path) && path.find('/')==std::string::npos)return path;throw std::runtime_error("更新版本记录无效");}
std::string Resolve(const std::string& base,const std::string& id){if(!SafeName(id) || id.find('/')!=std::string::npos)throw std::runtime_error("更新版本记录无效");if(id=="base")return base+"/base";if(id=="legacy")return base+"/active";return base+"/versions/"+id;}
void SetVersion(const std::string& base,const std::string& path){auto id=Identifier(base,path);Write(base+"/current.tmp",Bytes(id.begin(),id.end()));if(!MoveFileExW(Wide(base+"/current.tmp").c_str(),Wide(base+"/current.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法切换更新版本，原数据保留");}
}
std::string CardUpdatePlan::ActivePath(const std::string& root){auto base=Managed(root);if(!Exists(base+"/current.txt"))return base+"/active";auto bytes=Read(base+"/current.txt");return Resolve(base,std::string(bytes.begin(),bytes.end()));}
std::string CardUpdatePlan::Install(){
 if(cache.empty() || (!regular&&!pre))throw std::runtime_error("请先检查更新");auto base=Managed(root),active=ActivePath(root),stage=base+"/staging/"+Stamp(),version=base+"/versions/"+Stamp();Directory(stage);CopyTree(active,stage);
 if(regular){Copy(Exists(active+"/cards.cdb")?active+"/cards.cdb":root+"/cards.cdb",stage+"/cards.cdb");Merge(cache+"/regular.cdb",stage+"/cards.cdb");Cards(stage+"/cards.cdb");auto files=Zip(Read(cache+"/scripts.zip"),true);for(const auto& member:files)if(member.first.substr(member.first.size()-4)==".lua")Write(stage+"/script/"+member.first,member.second);}
 if(pre){Copy(cache+"/superpre.ypk",stage+"/superpre.ypk");auto files=Zip(Read(cache+"/superpre.ypk"),false);for(const auto& member:files)if(member.first.size()>=4 && member.first.substr(member.first.size()-4)==".lua" && member.first.find("script/")==0)Write(stage+"/pre-script/"+member.first.substr(7),member.second);}
 Directory(base+"/versions");Move(stage,version);std::string previous=Exists(active+"/cards.cdb") || Exists(active+"/superpre.ypk")?active:"base";
 auto previousID=Identifier(base,previous);Write(base+"/previous.txt",Bytes(previousID.begin(),previousID.end()));SetVersion(base,version);return previous=="base"?root:previous;
}
std::string CardUpdatePlan::Restore(const std::string& root){auto base=Managed(root);auto bytes=Read(base+"/previous.txt");std::string id(bytes.begin(),bytes.end());auto previous=Resolve(base,id);if(id!="base" && !Exists(previous))throw std::runtime_error("没有可恢复的更新备份");auto current=Identifier(base,ActivePath(root));SetVersion(base,id);Write(base+"/previous.txt",Bytes(current.begin(),current.end()));return id=="base"?root:previous;}
}
