#ifndef CARD_DOWNLOAD_H
#define CARD_DOWNLOAD_H
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>
namespace ygo {
struct CardDownloadProgress {std::string file;uint64_t downloaded{},total{},resumed{};double bytesPerSecond{};unsigned connections{};bool cached{};bool items{};};
using CardDownloadCallback=std::function<void(const CardDownloadProgress&)>;
class CardDownload {
public:
 static std::vector<unsigned char> Get(const std::string& url,const CardDownloadCallback& progress,std::atomic<bool>& cancel);
 static std::vector<unsigned char> Cached(const std::string& url,const std::string& directory,const CardDownloadCallback& progress,std::atomic<bool>& cancel);
 static std::string Describe(const CardDownloadProgress& progress);
};
}
#endif
