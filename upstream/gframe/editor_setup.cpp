#include "editor_setup.h"
#include "deck_manager.h"
#include "data_manager.h"
#include <IReadFile.h>
#include <sstream>
#include <cerrno>
#include <cstdlib>
namespace ygo {
bool LoadEditorDeck(const wchar_t* filename,DeckArray& deck,std::wstring& error) {
 auto* reader=DeckManager::OpenDeckReader(filename);
 if(!reader){error=L"无法打开卡组文件";return false;}
 if(reader->getSize()>1024*1024){reader->drop();error=L"卡组文件过大";return false;}
 std::string text(reader->getSize(),'\0');reader->read(&text[0],text.size());reader->drop();
 DeckArray loaded;std::istringstream stream(text);std::string line;bool side=false;
 while(std::getline(stream,line)) {
  if(line.empty() || line[0]=='#')continue;
  if(line[0]=='!'){side=true;continue;}if(side)continue;
  errno=0;char* end=nullptr;auto code=std::strtoull(line.c_str(),&end,10);
  while(end && (*end==' ' || *end=='\r' || *end=='\t'))++end;
  CardData data{};
  if(errno || !end || end==line.c_str() || *end || !code || code>0xffffffffULL || !dataManager.GetData(uint32_t(code),&data)){error=L"无法读取卡号："+std::wstring(line.begin(),line.end());return false;}
  if(data.type&TYPE_TOKEN){error=L"衍生物无法作为牌组卡片载入";return false;}
  auto& list=(data.type&TYPES_EXTRA_DECK)?loaded.extra:loaded.main;
  list.push_back(uint32_t(code));
  if(list.size()>254){error=L"当前引擎每个牌组区域最多容纳 254 张";return false;}
 }
 deck=std::move(loaded);return true;
}
}
