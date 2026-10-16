#include "card_images.h"
#include "image_manager.h"
#include "card_update.h"
#include "game.h"
#include "data_manager.h"
#include <windows.h>
#include <thread>
#include <mutex>
#include <memory>
#include <fstream>
#include <algorithm>
#include <chrono>
namespace ygo {
namespace {
irr::gui::IGUIWindow* window{};irr::gui::IGUICheckBox *official{},*pre{};irr::gui::IGUIStaticText *summary{},*status{};irr::gui::IGUIListBox* list{};
std::thread worker;std::mutex updateMutex;std::atomic<bool> busy{false},cancel{false},applying{false},reportReady{false};bool published=false;std::atomic<bool> requiresRestart{false};std::string message;CardDownloadProgress transfer;CardImageReport imageReport;bool imageMode=false;irr::gui::IGUIStaticText *progressText{},*progressFill{};CardUpdateReport result;std::unique_ptr<CardUpdatePlan> plan;
std::wstring W(const std::string& text){return BufferIO::DecodeUTF8String(text);}
void Publish(const std::string& text){std::lock_guard<std::mutex> lock(updateMutex);message=text;published=true;}
void Progress(const CardDownloadProgress& p){std::lock_guard<std::mutex> lock(updateMutex);transfer=p;message=p.items?p.file:(p.cached?p.file+" · 使用已校验缓存":"正在下载："+p.file);published=true;}
void Show(){if(mainGame->dInfo.isStarted)return;mainGame->wMainMenu->setVisible(false);window->setVisible(true);window->getParent()->bringToFront(window);}
void Run(bool install,bool restore=false,bool images=false){
 if(busy || mainGame->dInfo.isStarted)return;if(worker.joinable())worker.join();bool normal=official->isChecked(),early=pre->isChecked();if(!restore && !images && !normal && !early){status->setText(L"请至少选择一种卡片");return;}
 busy=true;cancel=false;applying=false;reportReady=false;{std::lock_guard<std::mutex> lock(updateMutex);result={};transfer={};imageReport={};imageMode=images;}summary->setText(restore?L"正在恢复上一版卡片数据…":L"正在检查，当前卡片数据保留…");list->clear();
 std::vector<CardImageEntry> cards;std::vector<std::string> archives;auto roots=ImageManager::CardImageRoots();
 if(images){for(const auto& row:dataManager.GetDataTable())if(row.second.type&7)cards.push_back({row.first,BufferIO::EncodeUTF8String(dataManager.GetName(row.first))});std::sort(cards.begin(),cards.end(),[](const CardImageEntry& a,const CardImageEntry& b){return a.code<b.code;});auto* fs=mainGame->device->getFileSystem();for(irr::u32 i=0;i<fs->getFileArchiveCount();++i)archives.push_back(fs->getFileArchive(i)->getArchiveName().c_str());}
 worker=std::thread([normal,early,install,restore,images,cards,roots,archives]{try{
  if(images){Publish("正在检查全部已加载卡片的图片…");auto report=CardImages::Repair(cards,roots,archives,Progress,cancel);{std::lock_guard<std::mutex> lock(updateMutex);imageReport=std::move(report);}Publish("卡图检查和补齐已完成；可立即显示，无需重启。详细结果见 .card-images/image-audit.tsv");}
  else if(restore){applying=true;CardUpdatePlan::Restore(".");{std::lock_guard<std::mutex> lock(updateMutex);requiresRestart=true;}Publish("已恢复上一版卡片数据，点击立即重启生效");}
  else{auto next=std::unique_ptr<CardUpdatePlan>(new CardUpdatePlan("."));
#ifdef DUEL_EDITOR_GUI_TEST
   if(const char* fixture=std::getenv("DUEL_EDITOR_UPDATE_FIXTURE")){CardDownloadProgress sample;sample.file="超先行卡片／卡图包";sample.total=22021541;sample.downloaded=sample.total/2+1;sample.connections=4;sample.bytesPerSecond=900*1024;Progress(sample);std::this_thread::sleep_for(std::chrono::milliseconds(650));next->Prepare(fixture,normal,early);}else
#endif
   next->Check(normal,early,Publish,cancel,Progress);{std::lock_guard<std::mutex> lock(updateMutex);result=next->report;reportReady=true;}
   if(install){if(cancel)throw std::runtime_error("已取消更新");applying=true;Publish("正在应用卡片、效果脚本和超先行卡图；保留更新前备份…");auto backup=next->Install();{std::lock_guard<std::mutex> lock(updateMutex);requiresRestart=true;}Publish("更新完成，点击立即重启生效。原卡片版本已备份。 "+backup);}
   else Publish("检查完成；列表显示缺少与有变更的卡片。可点击自动更新所选。");
   {std::lock_guard<std::mutex> lock(updateMutex);plan=std::move(next);}
  }
 }catch(const std::exception& error){Publish(std::string("更新未完成：")+error.what());}applying=false;busy=false;{std::lock_guard<std::mutex> lock(updateMutex);published=true;}});
}
}
void CardUpdater::Initialize(){auto* env=mainGame->env;window=env->addWindow({130,45,890,595},false,L"卡片更新 · 正式卡片与超先行",nullptr,9800);window->getCloseButton()->setVisible(false);window->setVisible(false);
 official=env->addCheckBox(true,{20,35,345,64},window,9801,L"正式卡片（中文数据库＋原版效果脚本）");pre=env->addCheckBox(true,{375,35,720,64},window,9802,L"MyCard 超先行（卡片＋效果＋卡图）");
 summary=env->addStaticText(L"检查将按卡号比较新增、文本／属性变更和脚本差异。",{20,78,720,157},false,true,window,9807);status=env->addStaticText(L"点击检查差异，或直接自动更新所选卡片。",{20,157,720,199},false,true,window,9808);list=env->addListBox({20,247,720,450},window,9809,true);list->setItemHeight(23);
 progressText=env->addStaticText(L"尚未开始下载",{20,200,720,223},false,false,window,9813);auto* bar=env->addStaticText(L"",{20,226,720,240},false,false,window,9814);bar->setDrawBackground(true);bar->setBackgroundColor(irr::video::SColor(255,190,190,190));progressFill=env->addStaticText(L"",{0,0,0,14},false,false,bar,9815);progressFill->setDrawBackground(true);progressFill->setBackgroundColor(irr::video::SColor(255,46,151,91));
 env->addButton({20,465,155,495},window,9803,L"检查差异");env->addButton({170,465,345,495},window,9804,L"自动更新所选");env->addButton({360,465,460,495},window,9805,L"取消下载");env->addButton({475,465,600,495},window,9810,L"恢复上一版");env->addButton({615,465,720,495},window,9811,L"立即重启");env->addButton({20,505,345,535},window,9812,L"检查／补齐全库卡图");env->addButton({360,505,720,535},window,9806,L"返回开始菜单");}
void CardUpdater::Tick(){if(!window)return;int cx=int(mainGame->window_size.Width)/2,cy=int(mainGame->window_size.Height)/2;window->setRelativePosition({cx-380,cy-275,cx+380,cy+275});std::lock_guard<std::mutex> lock(updateMutex);
 if(published){published=false;status->setText(W(message).c_str());if(!busy && imageMode){std::wstring text=L"已检查 "+std::to_wstring(imageReport.checked)+L" 张 · 已有可读图 "+std::to_wstring(imageReport.valid)+L" 张\n原缺图 "+std::to_wstring(imageReport.missing)+L" 张 · 损坏／不支持 "+std::to_wstring(imageReport.corrupt)+L" 张\n已补齐 "+std::to_wstring(imageReport.repaired)+L" 张 · 暂未取得 "+std::to_wstring(imageReport.unavailable)+L" 张";summary->setText(text.c_str());list->clear();for(const auto& issue:imageReport.issues){std::wstring row=W(issue.state)+L" · "+std::to_wstring(issue.code)+L" · "+W(issue.name);list->addItem(row.c_str());}}
 if(!busy && !reportReady && !imageMode){summary->setText(requiresRestart?L"卡片版本已切换，请重启后使用。":L"尚未完成差异检查；原卡片版本保留。");}
 if(!busy && reportReady){std::wstring text=L"本地源卡片 "+std::to_wstring(result.local)+L" · 正式源 "+std::to_wstring(result.regular)+L" · 超先行源 "+std::to_wstring(result.pre)+L"\n缺少 "+std::to_wstring(result.missing)+L" 张 · 有变更 "+std::to_wstring(result.changed)+L" 张 · 脚本差异 "+std::to_wstring(result.scriptChanges)+L" 个\n比较包含卡名、效果文本、属性、类型及 16 条效果提示。";summary->setText(text.c_str());list->clear();for(const auto& card:result.cards){std::wstring row=card.prerelease?L"[超先行] ":L"[正式] ";row+=card.modified?L"变更 · ":L"缺少 · ";row+=std::to_wstring(card.code)+L" · "+W(card.name);list->addItem(row.c_str());}}}
  if(transfer.total){int fill=int(std::min(uint64_t(700),transfer.downloaded*700/transfer.total));progressFill->setRelativePosition({0,0,fill,14});progressText->setText(W(CardDownload::Describe(transfer)).c_str());}
 else if(busy){static unsigned frame=0;int at=int(++frame%165)*4;progressFill->setRelativePosition({at,0,at+40,14});progressText->setText(transfer.downloaded?W(CardDownload::Describe(transfer)).c_str():L"正在连接／确认文件大小…");}
 else {progressFill->setRelativePosition({0,0,0,14});progressText->setText(L"尚未下载，或已完成当前操作");}
 for(int id:{9803,9804,9810,9801,9802,9812})window->getElementFromId(id,true)->setEnabled(!busy && !requiresRestart);window->getElementFromId(9805,true)->setEnabled(busy && !applying);window->getElementFromId(9811,true)->setEnabled(!busy && requiresRestart);window->getElementFromId(9806,true)->setEnabled(!busy && !requiresRestart);
}
bool CardUpdater::OnEvent(const irr::SEvent& event){if(!window || event.EventType!=irr::EET_GUI_EVENT)return false;int id=event.GUIEvent.Caller->getID();if(event.GUIEvent.EventType!=irr::gui::EGET_BUTTON_CLICKED)return false;
 if(id==106){Show();return true;}if(id==9812){Run(false,false,true);return true;}if(id==9803){Run(false);return true;}if(id==9804){Run(true);return true;}if(id==9805){cancel=true;Publish("正在取消下载，原卡片数据保留…");return true;}if(id==9810){Run(false,true);return true;}
 if(id==9806){if(!busy && !requiresRestart){window->setVisible(false);mainGame->wMainMenu->setVisible(true);}return true;}
 if(id==9811){if(!busy && requiresRestart){wchar_t executable[32768]{};if(GetModuleFileNameW(nullptr,executable,32768) && Game::SpawnAsync(executable,{}))mainGame->device->closeDevice();else status->setText(L"无法自动重启，请关闭程序后重新打开。");}return true;}
 if(id>=100 && id<=106 && (busy || requiresRestart)){Show();return true;}return false;
}
void CardUpdater::Shutdown(){cancel=true;if(worker.joinable())worker.join();}
}
