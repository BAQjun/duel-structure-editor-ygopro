#include "card_images.h"
#include <irrlicht.h>
#include <windows.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>
#include <set>
#include <chrono>
#include <memory>
#include <stdexcept>
namespace ygo {
namespace {
std::wstring W(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&w[0],n);return w;}
void Mk(const std::string& p){auto n=p.find_last_of('/');if(n!=std::string::npos && n>0 && GetFileAttributesW(W(p.substr(0,n)).c_str())==INVALID_FILE_ATTRIBUTES)Mk(p.substr(0,n));if(GetFileAttributesW(W(p).c_str())==INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(W(p).c_str(),nullptr))throw std::runtime_error("无法创建卡图目录");}
struct Device {irr::IrrlichtDevice* p{};Device():p(irr::createDevice(irr::video::EDT_NULL)){if(!p)throw std::runtime_error("无法初始化卡图检查");p->getLogger()->setLogLevel(irr::ELL_NONE);}~Device(){p->drop();}};
bool Valid(irr::IrrlichtDevice* device,irr::io::IReadFile* reader){if(!reader)return false;auto* image=device->getVideoDriver()->createImageFromFile(reader);if(!image)return false;auto size=image->getDimension();image->drop();return size.Width>=32 && size.Height>=48;}
bool Picture(Device& device,uint32_t code,const std::vector<std::string>& roots,bool& bad){for(const auto& root:roots)for(const char* ext:{".jpg",".png",".jpeg"}){auto* file=device.p->getFileSystem()->createAndOpenFile((root+"/"+std::to_string(code)+ext).c_str());if(!file)continue;bool ok=Valid(device.p,file);file->drop();if(ok)return true;bad=true;}return false;}
std::mutex completionMutex;std::vector<uint32_t> completed;std::set<uint32_t> repairedCodes;
void Download(Device& device,uint32_t code,std::atomic<bool>& cancel,const std::string& root){auto url="https://cdn.233.momobako.com/ygoimg/ygopro/"+std::to_string(code)+".webp!/format/jpg/fw/400/quality/90";auto bytes=CardDownload::Get(url,[](const CardDownloadProgress&){},cancel);if(bytes.size()>8*1024*1024 || bytes.size()<64)throw std::runtime_error("卡图大小异常");auto* memory=device.p->getFileSystem()->createMemoryReadFile(bytes.data(),long(bytes.size()),"card.jpg",false);bool ok=Valid(device.p,memory);memory->drop();if(!ok)throw std::runtime_error("卡图数据无法解码");auto dir=root+"/.card-images/pics";Mk(dir);auto file=dir+"/"+std::to_string(code)+".jpg";std::ofstream out(file+".partial",std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();if(!out || !MoveFileExW(W(file+".partial").c_str(),W(file).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("卡图保存失败");if(root=="."){std::lock_guard<std::mutex> lock(completionMutex);completed.push_back(code);repairedCodes.insert(code);}}
std::mutex queueMutex;std::condition_variable queueSignal;std::queue<uint32_t> queue;std::map<uint32_t,std::chrono::steady_clock::time_point> requested;std::vector<std::thread> networkWorkers;std::atomic<bool> stopping{false};
void Worker(){Device device;for(;;){uint32_t code=0;{std::unique_lock<std::mutex> lock(queueMutex);queueSignal.wait(lock,[]{return stopping || !queue.empty();});if(stopping)return;code=queue.front();queue.pop();}try{Download(device,code,stopping,".");}catch(...){}}}
}
void CardImages::Queue(uint32_t code){if(!code || code>0x0fffffff || stopping)return;std::lock_guard<std::mutex> lock(queueMutex);auto now=std::chrono::steady_clock::now();auto old=requested.find(code);if(old!=requested.end() && now-old->second<std::chrono::minutes(30))return;requested[code]=now;if(networkWorkers.empty())for(int i=0;i<2;++i)networkWorkers.emplace_back(Worker);queue.push(code);queueSignal.notify_one();}
bool CardImages::WasRepaired(uint32_t code){std::lock_guard<std::mutex> lock(completionMutex);return repairedCodes.count(code)!=0;}
std::vector<uint32_t> CardImages::TakeCompleted(){std::lock_guard<std::mutex> lock(completionMutex);std::vector<uint32_t> ready;ready.swap(completed);return ready;}
void CardImages::Shutdown(){stopping=true;queueSignal.notify_all();for(auto& worker:networkWorkers)if(worker.joinable())worker.join();networkWorkers.clear();}
CardImageReport CardImages::Repair(const std::vector<CardImageEntry>& cards,const std::vector<std::string>& roots,const std::vector<std::string>& archives,const CardDownloadCallback& progress,std::atomic<bool>& cancel,const std::string& outputRoot){
 Device device;for(const auto& archive:archives)device.p->getFileSystem()->addFileArchive(archive.c_str(),true,false,irr::io::EFAT_ZIP);CardImageReport report;CardDownloadProgress p;p.items=true;p.file="检查全部卡图（张）";p.total=cards.size();p.connections=1;std::vector<CardImageEntry> needed;
 for(const auto& card:cards){if(cancel)throw std::runtime_error("已取消卡图检查");bool bad=false;++report.checked;if(Picture(device,card.code,roots,bad))++report.valid;else{needed.push_back(card);if(bad)++report.corrupt;else ++report.missing;report.issues.push_back({card.code,card.name,bad?"损坏或不支持的图片":"缺少图片"});}p.downloaded=report.checked;if(report.checked%100==0 || report.checked==cards.size())progress(p);}
 p.file="补齐卡图（张）";p.total=needed.size();p.downloaded=0;p.connections=3;progress(p);std::atomic<size_t> next{0},finished{0};std::mutex reportMutex;auto started=std::chrono::steady_clock::now();std::vector<std::thread> workers;
 for(int n=0;n<3;++n)workers.emplace_back([&]{Device privateDevice;for(;;){size_t i=next++;if(i>=needed.size() || cancel)break;bool good=false;try{Download(privateDevice,needed[i].code,cancel,outputRoot);good=true;}catch(...){if(cancel)break;}
  {std::lock_guard<std::mutex> lock(reportMutex);report.issues[i].state=good?"已补齐":"图片源暂无图或下载失败";report.repaired+=good;report.unavailable+=!good;p.downloaded=++finished;p.bytesPerSecond=p.downloaded/std::max(0.01,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());progress(p);}
 }});
 for(auto& worker:workers)worker.join();Mk(outputRoot+"/.card-images");std::ofstream file(outputRoot+"/.card-images/image-audit.tsv",std::ios::binary|std::ios::trunc);file<<"code\tname\tstate\n";for(const auto& issue:report.issues)file<<issue.code<<'\t'<<issue.name<<'\t'<<issue.state<<'\n';if(cancel)throw std::runtime_error("已取消补图，已完成图片保留");return report;
}
}
