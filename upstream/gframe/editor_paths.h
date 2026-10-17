#ifndef YGO_EDITOR_PATHS_H
#define YGO_EDITOR_PATHS_H
#include <windows.h>
#include <string>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <vector>
namespace ygo { namespace EditorPaths {
inline std::wstring FullPath(const std::wstring& path) {
 DWORD size=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
 if(!size)return path;
 std::vector<wchar_t> buffer(size);
 DWORD written=GetFullPathNameW(path.c_str(),size,buffer.data(),nullptr);
 return written&&written<size?std::wstring(buffer.data(),written):path;
}
inline std::wstring RuntimeRoot() {
 std::vector<wchar_t> path(32768);
 DWORD size=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
 std::wstring result(path.data(),size);
 auto slash=result.find_last_of(L"/\\");
 return slash==std::wstring::npos?FullPath(L"."):result.substr(0,slash);
}
inline bool Absolute(const std::wstring& path) {
 return (path.size()>2&&path[1]==L':'&&(path[2]==L'/'||path[2]==L'\\')) || (!path.empty()&&(path[0]==L'/'||path[0]==L'\\'));
}
inline std::wstring FromUtf8(const std::string& path) {
 int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),int(path.size()),nullptr,0);
 std::wstring result(size,L'\0');
 if(size)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),int(path.size()),&result[0],size);
 return result;
}
inline std::wstring SaveRoot(const std::wstring& runtime) {
 std::wstring file=runtime+L"/editor-data.conf";
 std::ifstream conf(file.c_str());std::string path;std::getline(conf,path);
 if(path.compare(0,3,"\xef\xbb\xbf")==0)path.erase(0,3);
 auto begin=path.find_first_not_of(" \t\r\n"),end=path.find_last_not_of(" \t\r\n");
 path=begin==std::string::npos?"userdata/saves":path.substr(begin,end-begin+1);
 std::wstring decoded=FromUtf8(path);
 if(decoded.empty())decoded=L"userdata/saves";
 if(!Absolute(decoded))decoded=runtime+L"/"+decoded;
 return FullPath(decoded);
}
inline bool CreateDirectories(std::wstring path) {
 std::replace(path.begin(),path.end(),L'\\',L'/');
 size_t start=path.size()>2&&path[1]==L':'?3:1;
 if(path.compare(0,2,L"//")==0){auto server=path.find(L'/',2);auto share=server==std::wstring::npos?server:path.find(L'/',server+1);start=share==std::wstring::npos?path.size():share+1;}
 for(size_t i=start;i<=path.size();++i)if(i==path.size()||path[i]==L'/'){
  auto part=path.substr(0,i);if(part.empty())continue;
  if(!CreateDirectoryW(part.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
 }
 return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES&&(GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_DIRECTORY);
}
} }
#endif
