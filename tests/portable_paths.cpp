#include "../upstream/gframe/editor_paths.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
using namespace ygo;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);std::cout<<"PASS "<<text<<std::endl;}
void config(const std::wstring& root,const std::string& text){std::ofstream f((root+L"/editor-data.conf").c_str(),std::ios::binary);f<<text;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("Supply a fixture directory");
 auto root=EditorPaths::FullPath(argv[1]);check(EditorPaths::CreateDirectories(root),"Unicode fixture directory creates");DeleteFileW((root+L"/editor-data.conf").c_str());
 auto defaultRoot=EditorPaths::SaveRoot(root);check(defaultRoot==EditorPaths::FullPath(root+L"/userdata/saves"),"No config defaults beneath executable directory");
 config(root," \xef\xbb\xbf");config(root,"\xef\xbb\xbf  ../\xe5\x85\xb1\xe4\xba\xab\xe5\xad\x98\xe6\xa1\xa3/with spaces \r\n");auto relative=EditorPaths::SaveRoot(root);check(relative==EditorPaths::FullPath(root+L"/../共享存档/with spaces"),"UTF8 BOM whitespace Unicode and relative path resolve");check(EditorPaths::CreateDirectories(relative),"Nested relative Unicode save directory creates");
 config(root,"C:/Portable Test/saves\n");check(EditorPaths::SaveRoot(root)==L"C:\\Portable Test\\saves","Other drive absolute path is retained without redirecting to development folder");
 config(root,"//server/share/saves\n");check(EditorPaths::SaveRoot(root)==L"\\\\server\\share\\saves","UNC configuration is retained without contacting share");
 config(root,"\n");check(EditorPaths::SaveRoot(root)==defaultRoot,"Blank config falls back to portable location");
 auto before=EditorPaths::RuntimeRoot();SetCurrentDirectoryW(root.c_str());check(EditorPaths::RuntimeRoot()==before,"Executable directory is independent of launch working directory");
 std::cout<<"SUCCESS portable save path regression"<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
