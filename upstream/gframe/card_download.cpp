#include "card_download.h"
#include <windows.h>
#include <winhttp.h>
#include <zlib.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <cstdlib>
namespace ygo {
namespace {
using Bytes=std::vector<unsigned char>;constexpr size_t Limit=256*1024*1024,Chunk=1024*1024;
std::wstring W(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&r[0],n);return r;}
std::string S(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),&r[0],n,nullptr,nullptr);return r;}
void Mk(const std::string& p){auto n=p.find_last_of("/\\");if(n!=std::string::npos && n>0 && GetFileAttributesW(W(p.substr(0,n)).c_str())==INVALID_FILE_ATTRIBUTES)Mk(p.substr(0,n));if(GetFileAttributesW(W(p).c_str())==INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(W(p).c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("无法创建下载缓存目录");}
Bytes File(const std::string& p){std::ifstream f(p,std::ios::binary);if(!f)return {};f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0 || n>Limit)return {};f.seekg(0);Bytes r(size_t(n),0);if(n && !f.read(reinterpret_cast<char*>(r.data()),n))return {};return r;}
void Put(const std::string& p,const Bytes& b){std::ofstream f(p+".tmp",std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();if(!f || !MoveFileExW(W(p+".tmp").c_str(),W(p).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法保存下载缓存");}
std::string Key(const std::string& s){uint64_t h=1469598103934665603ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}std::ostringstream out;out<<std::hex<<h;return out.str();}
std::string Name(const std::string& url){auto at=url.find_last_of('/');return at==std::string::npos?url:url.substr(at+1);}
struct Handle {HINTERNET h{};explicit Handle(HINTERNET value):h(value){if(!h)throw std::runtime_error("无法连接下载服务器");}~Handle(){WinHttpCloseHandle(h);}Handle(const Handle&)=delete;};
class Request {
 std::wstring proxy,address;std::unique_ptr<Handle> session,connection,request;
public:
 DWORD status{};
 Request(const std::string& url,const std::string& extra,std::atomic<bool>& cancel){if(cancel)throw std::runtime_error("已取消下载");address=W(url);URL_COMPONENTSW u{};u.dwStructSize=sizeof u;u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=DWORD(-1);if(!WinHttpCrackUrl(address.c_str(),DWORD(address.size()),0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS)throw std::runtime_error("下载源必须使用 HTTPS");
  if(const char* env=std::getenv("HTTPS_PROXY")){proxy=W(env);auto pos=proxy.find(L"://");if(pos!=std::wstring::npos)proxy=proxy.substr(pos+3);while(!proxy.empty()&&proxy.back()==L'/')proxy.pop_back();}
  session.reset(new Handle(WinHttpOpen(L"DuelStructureEditor/0.1.7",proxy.empty()?WINHTTP_ACCESS_TYPE_DEFAULT_PROXY:WINHTTP_ACCESS_TYPE_NAMED_PROXY,proxy.empty()?WINHTTP_NO_PROXY_NAME:proxy.c_str(),WINHTTP_NO_PROXY_BYPASS,0)));WinHttpSetTimeouts(session->h,6000,10000,10000,10000);
  std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);if(u.dwExtraInfoLength)path.append(u.lpszExtraInfo,u.dwExtraInfoLength);connection.reset(new Handle(WinHttpConnect(session->h,host.c_str(),u.nPort,0)));request.reset(new Handle(WinHttpOpenRequest(connection->h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)));auto headers=W(extra);
  if(!WinHttpSendRequest(request->h,headers.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:headers.c_str(),DWORD(headers.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request->h,nullptr))throw std::runtime_error("下载连接失败，请重试");DWORD len=sizeof status;if(!WinHttpQueryHeaders(request->h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&len,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("无法读取下载响应");
 }
 std::string Header(DWORD code){DWORD size=0;WinHttpQueryHeaders(request->h,code,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&size,WINHTTP_NO_HEADER_INDEX);if(!size)return {};std::wstring value(size/sizeof(wchar_t),0);if(!WinHttpQueryHeaders(request->h,code,WINHTTP_HEADER_NAME_BY_INDEX,&value[0],&size,WINHTTP_NO_HEADER_INDEX))return {};value.resize(size/sizeof(wchar_t));while(!value.empty()&&value.back()==0)value.pop_back();return S(value);}
 Bytes Read(std::atomic<bool>& cancel,const std::function<void(size_t)>& progress,size_t expected=0,std::atomic<bool>* stop=nullptr){Bytes body;if(expected>Limit)throw std::runtime_error("下载文件超出大小限制");if(expected)body.reserve(expected);auto start=std::chrono::steady_clock::now();unsigned char block[65536];for(;;){if(cancel || (stop && *stop))throw std::runtime_error("已取消下载");if(std::chrono::steady_clock::now()-start>std::chrono::seconds(600))throw std::runtime_error("下载超时");DWORD size=0;if(!WinHttpReadData(request->h,block,sizeof block,&size))throw std::runtime_error("下载中断，已完成分段保留");if(!size)break;if(body.size()+size>Limit || (expected && body.size()+size>expected))throw std::runtime_error("下载响应长度异常");body.insert(body.end(),block,block+size);progress(body.size());}if(expected && body.size()!=expected)throw std::runtime_error("下载内容不完整");return body;}
};
uint64_t Number(const std::string& s){if(s.empty())return 0;try{return std::stoull(s);}catch(...){return 0;}}
std::string Mirror(const std::string& url,unsigned attempt){const std::string prefix="https://cdntx.moecube.com/";if(url.find(prefix)!=0)return url;const char* names[]={"cdntx3.moecube.com","cdntx2.moecube.com","cdntx.moecube.com"};return std::string("https://")+names[attempt%3]+url.substr(prefix.size()-1);}
struct Cache {std::string url,etag;Bytes bytes;uint32_t crc{};size_t size{};};
Cache CachedFile(const std::string& key){Cache c;std::ifstream meta(key+".meta");std::getline(meta,c.url);std::getline(meta,c.etag);meta>>c.size>>c.crc;if(meta && c.size<=Limit){c.bytes=File(key+".data");if(c.bytes.size()!=c.size || crc32(0,c.bytes.data(),c.bytes.size())!=c.crc)c.bytes.clear();}return c;}
void SaveCache(const std::string& key,const std::string& url,const std::string& etag,const Bytes& bytes){Put(key+".data",bytes);std::string meta=url+"\n"+etag+"\n"+std::to_string(bytes.size())+" "+std::to_string(crc32(0,bytes.data(),bytes.size()))+"\n";Put(key+".meta",Bytes(meta.begin(),meta.end()));}
Bytes Single(const std::string& url,const CardDownloadCallback& progress,std::atomic<bool>& cancel){std::exception_ptr failure;unsigned tries=url.find("https://cdntx.moecube.com/")==0?3:1;for(unsigned attempt=0;attempt<tries;++attempt){try{CardDownloadProgress p;p.file=Name(url);p.connections=1;progress(p);auto start=std::chrono::steady_clock::now();Request request(Mirror(url,attempt),"",cancel);if(request.status!=200)throw std::runtime_error("下载源返回 HTTP "+std::to_string(request.status));p.total=Number(request.Header(WINHTTP_QUERY_CONTENT_LENGTH));progress(p);auto bytes=request.Read(cancel,[&](size_t n){p.downloaded=n;double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();p.bytesPerSecond=n/std::max(elapsed,0.01);progress(p);},size_t(p.total));p.downloaded=p.total=bytes.size();progress(p);return bytes;}catch(...){failure=std::current_exception();if(cancel)std::rethrow_exception(failure);}}std::rethrow_exception(failure);}

}
std::string CardDownload::Describe(const CardDownloadProgress& p){if(p.items){std::ostringstream s;s<<p.file<<" · "<<p.downloaded<<" / "<<p.total;if(p.total)s<<" · "<<std::min(uint64_t(100),p.downloaded*100/p.total)<<"%";if(p.connections>1)s<<" · "<<p.connections<<" 路补图";return s.str();}if(p.cached)return p.file+" · 已复用校验缓存";std::ostringstream s;s<<p.file<<" · "<<std::fixed<<std::setprecision(1)<<p.downloaded/1048576.0<<" / ";if(p.total)s<<p.total/1048576.0<<" MB · "<<std::min(uint64_t(100),p.downloaded*100/p.total)<<"%";else s<<"总大小未知";s<<" · "<<p.bytesPerSecond/1024.0<<" KB/s";if(p.total>p.downloaded && p.bytesPerSecond>0)s<<" · 剩余约 "<<int((p.total-p.downloaded)/p.bytesPerSecond)<<" 秒";if(p.connections>1)s<<" · "<<p.connections<<" 路分段";if(p.resumed)s<<" · 复用分段 "<<p.resumed/1048576.0<<" MB";return s.str();}
std::vector<unsigned char> CardDownload::Get(const std::string& url,const CardDownloadCallback& progress,std::atomic<bool>& cancel){return Single(url,progress,cancel);}
std::vector<unsigned char> CardDownload::Cached(const std::string& url,const std::string& directory,const CardDownloadCallback& progress,std::atomic<bool>& cancel){
 Mk(directory);std::string key=directory+"/"+Key(url);auto cached=CachedFile(key);if(cached.url!=url)cached.bytes.clear();CardDownloadProgress p;p.file=Name(url);progress(p);
 if(!cached.bytes.empty() && url.find("/ygopro-super-pre/archive/ygopro-super-pre-")!=std::string::npos){p.downloaded=p.total=cached.bytes.size();p.cached=true;progress(p);return cached.bytes;}
 if(url.find("/format/jpg")!=std::string::npos){if(!cached.bytes.empty()){p.downloaded=p.total=cached.bytes.size();p.cached=true;progress(p);return cached.bytes;}auto bytes=Single(url,progress,cancel);SaveCache(key,url,"",bytes);return bytes;}
 std::string conditional="Range: bytes=0-0\r\n";if(!cached.bytes.empty() && !cached.etag.empty())conditional+="If-None-Match: "+cached.etag+"\r\n";
 std::unique_ptr<Request> probe;std::exception_ptr connectionError;for(unsigned i=0;i<3 && !probe;++i){try{probe.reset(new Request(Mirror(url,i),conditional,cancel));if(probe->status!=200 && probe->status!=206 && probe->status!=304){probe.reset();throw std::runtime_error("下载源响应异常");}}catch(...){connectionError=std::current_exception();if(cancel)std::rethrow_exception(connectionError);}}
 if(!probe){if(connectionError)std::rethrow_exception(connectionError);throw std::runtime_error("无法连接下载源");}
 std::string etag=probe->Header(WINHTTP_QUERY_ETAG);if(!cached.bytes.empty() && (probe->status==304 || (!etag.empty() && etag==cached.etag))){p.downloaded=p.total=cached.bytes.size();p.cached=true;progress(p);return cached.bytes;}
 if(probe->status==304)throw std::runtime_error("下载缓存校验失败");
 if(probe->status==206){std::string range=probe->Header(WINHTTP_QUERY_CONTENT_RANGE);auto slash=range.find('/');if(slash!=std::string::npos)p.total=Number(range.substr(slash+1));}else p.total=Number(probe->Header(WINHTTP_QUERY_CONTENT_LENGTH));
 progress(p);
 if(!p.total && probe->status==200){p.connections=1;auto started=std::chrono::steady_clock::now();auto bytes=probe->Read(cancel,[&](size_t n){p.downloaded=n;p.bytesPerSecond=n/std::max(0.01,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());progress(p);});p.downloaded=p.total=bytes.size();progress(p);SaveCache(key,url,etag,bytes);return bytes;}
 if(!p.total || p.total>Limit)throw std::runtime_error("下载文件大小异常");
 if(probe->status==200 || p.total<Chunk){if(probe->status==206){probe.reset();auto bytes=Single(url,progress,cancel);SaveCache(key,url,etag,bytes);return bytes;}p.connections=1;auto start=std::chrono::steady_clock::now();auto bytes=probe->Read(cancel,[&](size_t n){p.downloaded=n;p.bytesPerSecond=n/std::max(0.01,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());progress(p);},size_t(p.total));SaveCache(key,url,etag,bytes);return bytes;}
 probe.reset();size_t parts=(size_t(p.total)+Chunk-1)/Chunk;std::string prefix=key+"-"+Key(etag+std::to_string(p.total));std::vector<Bytes> blocks(parts);std::vector<size_t> received(parts,0);std::atomic<size_t> next{0};std::atomic<bool> stop{false};std::exception_ptr failure;std::mutex state;auto started=std::chrono::steady_clock::now();uint64_t resumed=0;p.connections=std::min(size_t(4),parts);
 for(size_t i=0;i<parts;++i){std::string part=prefix+"-"+std::to_string(i);auto b=File(part);std::ifstream checksum(part+".crc");uint32_t crc=0;checksum>>crc;size_t length=std::min(Chunk,size_t(p.total)-i*Chunk);if(checksum && b.size()==length && crc32(0,b.data(),b.size())==crc){blocks[i]=std::move(b);received[i]=length;resumed+=length;}}
 p.resumed=resumed;
 auto publish=[&](size_t index,size_t bytes){std::lock_guard<std::mutex> lock(state);received[index]=bytes;p.downloaded=0;for(auto n:received)p.downloaded+=n;p.bytesPerSecond=(p.downloaded>=resumed?p.downloaded-resumed:0)/std::max(0.01,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());progress(p);};
 std::vector<std::thread> workers;for(unsigned thread=0;thread<p.connections;++thread)workers.emplace_back([&,thread]{try{for(;;){size_t i=next++;if(i>=parts || stop || cancel)break;if(!blocks[i].empty()){publish(i,blocks[i].size());continue;}size_t begin=i*Chunk,end=std::min(size_t(p.total),begin+Chunk)-1;bool complete=false;std::exception_ptr error;
   for(unsigned attempt=0;attempt<3 && !complete;++attempt){try{Request request(Mirror(url,thread+attempt),"Range: bytes="+std::to_string(begin)+"-"+std::to_string(end)+"\r\n",cancel);std::string expected="bytes "+std::to_string(begin)+"-"+std::to_string(end)+"/"+std::to_string(p.total);auto actual=request.Header(WINHTTP_QUERY_CONTENT_RANGE),actualTag=request.Header(WINHTTP_QUERY_ETAG);if(request.status!=206 || actual!=expected || (!etag.empty()&&!actualTag.empty()&&etag!=actualTag))throw std::runtime_error("分段响应不匹配");auto b=request.Read(cancel,[&](size_t n){publish(i,n);},end-begin+1,&stop);std::string part=prefix+"-"+std::to_string(i);Put(part,b);auto crc=std::to_string(crc32(0,b.data(),b.size()));Put(part+".crc",Bytes(crc.begin(),crc.end()));blocks[i]=std::move(b);complete=true;}catch(...){error=std::current_exception();if(cancel || stop)std::rethrow_exception(error);}}
   if(!complete)std::rethrow_exception(error);
  }}catch(...){std::lock_guard<std::mutex> lock(state);if(!failure)failure=std::current_exception();stop=true;}});
 for(auto& worker:workers)worker.join();if(cancel)throw std::runtime_error("已取消下载，完成的分段保留");if(failure){p.file="分段不可用，改用单连接 · "+Name(url);progress(p);auto b=Single(url,progress,cancel);SaveCache(key,url,etag,b);return b;}
 Bytes merged;merged.reserve(size_t(p.total));for(auto& block:blocks)merged.insert(merged.end(),block.begin(),block.end());if(merged.size()!=p.total)throw std::runtime_error("分段合并长度异常");SaveCache(key,url,etag,merged);p.downloaded=p.total;progress(p);return merged;
}
}
