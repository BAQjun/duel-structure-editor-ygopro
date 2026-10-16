#ifndef CARD_IMAGES_H
#define CARD_IMAGES_H
#include "card_download.h"
#include <vector>
#include <string>
#include <atomic>
namespace ygo {
struct CardImageEntry {uint32_t code{};std::string name;};
struct CardImageIssue {uint32_t code{};std::string name;std::string state;};
struct CardImageReport {size_t checked{},valid{},missing{},corrupt{},repaired{},unavailable{};std::vector<CardImageIssue> issues;};
class CardImages {
public:
 static CardImageReport Repair(const std::vector<CardImageEntry>& cards,const std::vector<std::string>& roots,const std::vector<std::string>& archives,const CardDownloadCallback& progress,std::atomic<bool>& cancel,const std::string& outputRoot=".");
 static void Queue(uint32_t code);static bool WasRepaired(uint32_t code);static std::vector<uint32_t> TakeCompleted();static void Shutdown();
};
}
#endif
