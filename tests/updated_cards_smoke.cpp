#include "../upstream/gframe/card_update.h"
#include "../upstream/gframe/game.h"
#include "../upstream/gframe/data_manager.h"
#include "../upstream/ocgcore/duel.h"
#include "../upstream/ocgcore/card.h"
#include "../upstream/ocgcore/ocgapi.h"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
using namespace ygo;
int errors=0;uint32_t current=0;
uint32_t Log(intptr_t duel,uint32_t type){if(type!=2){++errors;char msg[4096]{};get_log_message(duel,msg);std::cerr<<"SCRIPT "<<current<<" "<<msg<<std::endl;}return 0;}
void DB(const char* path){sqlite3* db=nullptr;if(sqlite3_open(path,&db)!=SQLITE_OK || !dataManager.ReadDB(db))throw std::runtime_error("Cannot read updated DB");sqlite3_close(db);}
int main(){try{mainGame=new Game();auto* device=irr::createDevice(irr::video::EDT_NULL);dataManager.IrrFileSystem=device->getFileSystem();DB((CardUpdatePlan::ActivePath(".")+"/cards.cdb").c_str());DB("../../work/update-research/test-release.cdb");DB("../../work/update-research/test-update.cdb");set_card_reader(DataManager::CardReader);set_script_reader(DataManager::ScriptReaderEx);set_message_handler(Log);auto handle=create_duel(1234);auto* duel=reinterpret_cast<::duel*>(handle);int count=0,metadata=0;for(const auto& row:dataManager.GetDataTable()){if(!(row.second.type&7)){++metadata;continue;}current=row.first;duel->new_card(row.first);++count;}std::cout<<"INITIALIZATION cards="<<count<<" metadata="<<metadata<<" errors="<<errors<<std::endl;end_duel(handle);device->drop();delete mainGame;return errors?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
