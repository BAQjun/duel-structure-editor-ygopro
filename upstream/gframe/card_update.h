#ifndef CARD_UPDATE_H
#define CARD_UPDATE_H
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <IEventReceiver.h>
#include "card_download.h"
namespace ygo {
struct CardUpdateDifference {uint32_t code{};std::string name;bool modified{};bool prerelease{};};
struct CardUpdateReport {size_t local{},regular{},pre{},missing{},changed{},scriptChanges{};std::vector<CardUpdateDifference> cards;};
class CardUpdatePlan {
 std::string root,cache;bool regular=false,pre=false;std::string packUrl;
public:
 CardUpdateReport report;
 explicit CardUpdatePlan(const std::string& directory):root(directory){}
 void Check(bool official,bool prerelease,const std::function<void(const std::string&)>& progress,std::atomic<bool>& cancel,const CardDownloadCallback& details={});
 void Prepare(const std::string& fixtures,bool official,bool prerelease);
 std::string Install();
 static std::string ActivePath(const std::string& root);
 static std::string Restore(const std::string& root);
};
class CardUpdater {
public:
 static void Initialize();static bool OnEvent(const irr::SEvent& event);static void Tick();static void Shutdown();
};
}
#endif
