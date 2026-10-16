#include <algorithm>
#include "config.h"
#include "replay.h"
#include "file_system.h"
#include "deck_manager.h"

#define LZMA_API_STATIC
#include <lzma.h>

namespace ygo {

Replay::Replay() {
	replay_data = new unsigned char[MAX_REPLAY_SIZE];
	comp_data = new unsigned char[MAX_COMP_SIZE];
}
Replay::~Replay() {
	delete[] replay_data;
	delete[] comp_data;
}
void Replay::BeginRecord(bool inMemory) {
	if(!FileSystem::IsDirExists(L"./replay") && !FileSystem::MakeDir(L"./replay"))
		return;
	if(is_recording && fp)
		std::fclose(fp);
	fp = inMemory?nullptr:myfopen("./replay/_LastReplay.yrp", "wb");
	if(!inMemory && !fp)
		return;
	Reset();
	is_recording = true;
}
void Replay::WriteHeader(ExtendedReplayHeader& header) {
	pheader = header;
	if(fp){std::fwrite(&header, sizeof header, 1, fp);std::fflush(fp);}
}
void Replay::WriteData(const void* data, size_t length, bool flush) {
	if(!is_recording)
		return;
	if (length > MAX_REPLAY_SIZE - replay_size)
		return;
	std::memcpy(replay_data + replay_size, data, length);
	replay_size += length;
	if(fp)std::fwrite(data, length, 1, fp);
	if(flush && fp)
		std::fflush(fp);
}
void Replay::WriteInt32(int32_t data, bool flush) {
	Write<int32_t>(data, flush);
}
size_t Replay::WriteResponse(const void* data, size_t length) {
	if(!is_recording || !data)
		return 0;
	const size_t response_length = std::min<size_t>(length, UINT8_MAX);
	if(!response_length)
		return 0;
	const size_t total_length = sizeof(uint8_t) + response_length;
	if(total_length > MAX_REPLAY_SIZE - replay_size)
		return 0;
	Write<uint8_t>(static_cast<uint8_t>(response_length), false);
	WriteData(data, response_length);
	return total_length;
}
bool Replay::ReplaceRecording(const ExtendedReplayHeader& header,const std::vector<unsigned char>& data) {
 if(data.size()>MAX_REPLAY_SIZE)return false;
 bool inMemory=is_recording && !fp;auto h=header;BeginRecord(inMemory);if(!is_recording)return false;WriteHeader(h);WriteData(data.data(),data.size());return true;
}
bool Replay::CanWriteEditorCommand() const {
 return is_recording && (pheader.base.flag&REPLAY_EDITOR)
   && sizeof(editor_command)+6<=MAX_REPLAY_SIZE-replay_size;
}
bool Replay::WriteEditorCommand(const editor_command& command,uint32_t processPoint) {
 if(!CanWriteEditorCommand())return false;
 Write<uint8_t>(0,false); // reserved; normal responses are 1..SIZE_RETURN_VALUE bytes
 if(pheader.value1>=5)Write<uint32_t>(processPoint,false);
 Write<uint8_t>(uint8_t(command.version),false);
 const uint32_t values[]={command.version,command.opcode,command.revision,command.code,
  command.owner,command.player,command.location,command.sequence,command.position,command.value,command.target_low,command.target_high,command.form,command.race,command.attribute,command.attack,command.defense,command.level,command.monster_flags,command.self_summon,command.states,command.restrictions};
 for(size_t i=0;i<(command.version==1?10U:command.version==2?12U:22U);++i) {auto value=values[i];
  const uint8_t bytes[]={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};
  WriteData(bytes,4,false);
 }
 Flush();return true;
}
bool Replay::RemoveData(size_t length) {
	if(!is_recording)
		return false;
	if(length > replay_size)
		return false;
	replay_size -= length;
	if(fp) {
		std::fflush(fp);
		std::fseek(fp, -static_cast<long>(length), SEEK_CUR);
	}
	return true;
}
void Replay::Flush() {
	if(!is_recording || !fp)
		return;
	std::fflush(fp);
}
void Replay::EndRecord() {
	if(!is_recording)
		return;
	if(fp){std::fclose(fp);fp=nullptr;}
	pheader.base.datasize = replay_size;
	pheader.base.flag |= REPLAY_COMPRESSED;
	lzma_options_lzma opt;
	lzma_lzma_preset(&opt, 5);
	opt.dict_size = 0x1U << 24;
	opt.lc = 3;
	opt.lp = 0;
	opt.pb = 2;
	opt.nice_len = 32;
	opt.mf = LZMA_MF_BT4;
	opt.ext_flags = 0;
	lzma_filter filters[2];
	filters[0].id = LZMA_FILTER_LZMA1EXT;
	filters[0].options = &opt;
	filters[1].id = LZMA_VLI_UNKNOWN;
	filters[1].options = nullptr;
	lzma_properties_encode(&filters[0], pheader.base.props);
	size_t out_pos = 0;
	lzma_ret lret = lzma_raw_buffer_encode(filters, nullptr, replay_data, replay_size, comp_data, &out_pos, MAX_COMP_SIZE);
	if (lret != LZMA_OK) {
		std::memcpy(comp_data, &lret, sizeof lret);
		comp_size = sizeof lret;
	} else {
		comp_size = out_pos;
	}
	is_recording = false;
}
bool Replay::SaveReplay(const wchar_t* base_name) {
	if(!FileSystem::IsDirExists(L"./replay") && !FileSystem::MakeDir(L"./replay"))
		return false;
	wchar_t filename[256]{};
	wchar_t path[256]{};
	BufferIO::CopyWideString(base_name, filename);
	FileSystem::SafeFileName(filename);
	if (myswprintf(path, L"./replay/%ls.yrp", filename) <= 0)
		return false;
	FILE* rfp = mywfopen(path, "wb");
	if(!rfp)
		return false;
	std::fwrite(&pheader, sizeof pheader, 1, rfp);
	std::fwrite(comp_data, comp_size, 1, rfp);
	std::fclose(rfp);
	return true;
}
bool Replay::OpenReplay(const wchar_t* name) {
	FILE* rfp = mywfopen(name, "rb");
	if(!rfp) {
		wchar_t fname[256];
		if (myswprintf(fname, L"./replay/%ls", name) <= 0)
			return false;
		rfp = mywfopen(fname, "rb");
	}
	if(!rfp)
		return false;

	Reset();
	bool correct_header = true;
	if (std::fread(&pheader, sizeof pheader.base, 1, rfp) < 1)
		correct_header = false;
	else if (pheader.base.id != REPLAY_ID_YRP1 && pheader.base.id != REPLAY_ID_YRP2)
		correct_header = false;
	else if (pheader.base.version < 0x12d0u)
		correct_header = false;
	else if (pheader.base.version >= 0x1353u && !(pheader.base.flag & REPLAY_UNIFORM))
		correct_header = false;
	if (!correct_header) {
		std::fclose(rfp);
		return false;
	}
	if (pheader.base.id == REPLAY_ID_YRP2 && std::fread(reinterpret_cast<unsigned char*>(&pheader) + sizeof pheader.base, sizeof pheader - sizeof pheader.base, 1, rfp) < 1) {
		std::fclose(rfp);
		return false;
	}
	if(pheader.base.flag & REPLAY_COMPRESSED) {
		comp_size = std::fread(comp_data, 1, MAX_COMP_SIZE, rfp);
		std::fclose(rfp);
		if (pheader.base.datasize > MAX_REPLAY_SIZE)
			return false;
		replay_size = pheader.base.datasize;
		lzma_filter filter;
		filter.id = LZMA_FILTER_LZMA1;
		filter.options = nullptr;
		if (lzma_properties_decode(&filter, nullptr, pheader.base.props, 5) != LZMA_OK)
			return false;
		lzma_options_lzma *lzma_opt = static_cast<lzma_options_lzma *>(filter.options);
		lzma_set_ext_size(*lzma_opt, replay_size);
		lzma_opt->ext_flags = LZMA_LZMA1EXT_ALLOW_EOPM;
		filter.id = LZMA_FILTER_LZMA1EXT;
		lzma_filter filters[2] = { filter, { LZMA_VLI_UNKNOWN, nullptr } };
		size_t in_pos = 0, out_pos = 0;
		lzma_ret lret = lzma_raw_buffer_decode(filters, nullptr, comp_data, &in_pos, comp_size, replay_data, &out_pos, replay_size);
		lzma_filters_free(filters, nullptr);
		if (lret != LZMA_OK || out_pos != replay_size) {
			replay_size = 0;
			return false;
		}
	} else {
		replay_size = std::fread(replay_data, 1, MAX_REPLAY_SIZE, rfp);
		std::fclose(rfp);
		comp_size = 0;
	}
	is_replaying = true;
	can_read = true;
	if (!ReadInfo()) {
		Reset();
		return false;
	}
	info_offset = data_position;
	data_position = 0;
	return true;
}
bool Replay::DeleteReplay(const wchar_t* name) {
	if (std::wcschr(name, L'/') || std::wcschr(name, L'\\'))
		return false;
	wchar_t fname[256];
	if(myswprintf(fname, L"./replay/%ls", name) <= 0)
		return false;
	return FileSystem::RemoveFile(fname);
}
bool Replay::RenameReplay(const wchar_t* oldname, const wchar_t* newname) {
	wchar_t old_path[256];
	wchar_t new_path[256];
	if (std::wcschr(oldname, L'/') || std::wcschr(oldname, L'\\'))
		return false;
	if (std::wcschr(newname, L'/') || std::wcschr(newname, L'\\'))
		return false;
	if (myswprintf(old_path, L"./replay/%ls", oldname) <= 0)
		return false;
	if (myswprintf(new_path, L"./replay/%ls", newname) <= 0)
		return false;
	return FileSystem::Rename(old_path, new_path);
}
bool Replay::EditDue(uint32_t point) const{
 if(pheader.value1<5 || data_position+6>replay_size || replay_data[data_position]!=0)return false;
 uint32_t recorded=0;std::memcpy(&recorded,replay_data+data_position+1,4);return recorded==point;
}
bool Replay::ReadNextInput(unsigned char resp[],editor_command& command,bool& is_edit) {
 is_edit=false;uint8_t len=0;
 if(!ReadData(&len,1))return false;
 if(len) {
  if(len>SIZE_RETURN_VALUE)return false;

  std::memset(resp,0,SIZE_RETURN_VALUE);
  return ReadData(resp,len);
 }
 if(!(pheader.base.flag&REPLAY_EDITOR)||(pheader.value1<1 || pheader.value1>6))return false;
 if(pheader.value1>=5){uint32_t point=0;if(!ReadData(&point,4))return false;}
 uint8_t version=0;
 if(!ReadData(&version,1)||(version<1 || version>9) || (version==2 && pheader.value1<2) || (version>=3 && pheader.value1<4))return false;
 uint32_t values[22]{};
 for(unsigned int i=0;i<(version==1?10U:version==2?12U:22U);++i) {
  auto& value=values[i];
  uint8_t bytes[4]{};if(!ReadData(bytes,4))return false;
  value=uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16)|(uint32_t(bytes[3])<<24);
 }
 command.version=values[0];command.opcode=values[1];command.revision=values[2];command.code=values[3];
 command.owner=values[4];command.player=values[5];command.location=values[6];command.sequence=values[7];
 command.position=values[8];command.value=values[9];
 command.target_low=values[10];command.target_high=values[11];
 command.form=values[12];command.race=values[13];command.attribute=values[14];command.attack=values[15];command.defense=values[16];command.level=values[17];command.monster_flags=values[18];command.self_summon=values[19];command.states=values[20];command.restrictions=values[21];
 if(command.version!=version)return false;
 is_edit=true;return true;
}
bool Replay::ReadNextResponse(unsigned char resp[]) {
 editor_command command{};bool is_edit=false;
 return ReadNextInput(resp,command,is_edit)&&!is_edit;
}
bool Replay::ReadName(wchar_t* data) {
	uint16_t buffer[20]{};
	if (!ReadData(buffer, sizeof buffer)) {
		return false;
	}
	BufferIO::CopyWStr(buffer, data, 20);
	return true;
}
void Replay::ReadHeader(ExtendedReplayHeader& header) {
	header = pheader;
}
bool Replay::ReadData(void* data, size_t length) {
	if (!is_replaying || !can_read)
		return false;
	if (length > replay_size - data_position) {
		can_read = false;
		return false;
	}
	if (length)
		std::memcpy(data, replay_data + data_position, length);
	data_position += length;
	return true;
}
int32_t Replay::ReadInt32() {
	return Read<int32_t>();
}
void Replay::Rewind() {
	data_position = 0;
	can_read = true;
}
void Replay::Reset() {
	is_recording = false;
	is_replaying = false;
	can_read = false;
	replay_size = 0;
	comp_size = 0;
	data_position = 0;
	info_offset = 0;
	players.clear();
	params = { 0 };
	decks.clear();
	script_name.clear();editor_start_hand=0;editor_start_hand1=0xffffffffU;
}
void Replay::SkipInfo(){
	if (data_position == 0)
		data_position += info_offset;
}
bool Replay::IsReplaying() const {
	return is_replaying;
}
bool Replay::SaveDeck(size_t index, const wchar_t* filename) {
	if(index >= decks.size())
		return false;
	DeckArray deck = decks[index];
 if(!((pheader.base.flag&REPLAY_EDITOR) && pheader.value1>=3)) {
	std::reverse(deck.main.begin(), deck.main.end());
	std::reverse(deck.extra.begin(), deck.extra.end());
 }
	return DeckManager::SaveDeckArray(deck, filename);
}
bool Replay::ReadInfo() {
	int player_count = (pheader.base.flag & REPLAY_TAG) ? 4 : 2;
	for (int i = 0; i < player_count; ++i) {
		wchar_t name[20]{};
		if (!ReadName(name))
			return false;
		players.push_back(name);
	}
	if (!ReadData(&params, sizeof params))
		return false;
	bool is_tag1 = pheader.base.flag & REPLAY_TAG;
	bool is_tag2 = params.duel_flag & DUEL_TAG_MODE;
	if (is_tag1 != is_tag2)
		return false;
	if (pheader.base.flag & REPLAY_SINGLE_MODE) {
		uint16_t slen = Read<uint16_t>();
		char filename[256]{};
		if (slen == 0 || slen > sizeof(filename) - 1)
			return false;
		if (!ReadData(filename, slen))
			return false;
		filename[slen] = 0;
		if (std::strncmp(filename, "./single/", 9))
			return false;
		script_name = filename + 9;
		if (script_name.find_first_of(R"(/\)") != std::string::npos)
			return false;
  if((pheader.base.flag&REPLAY_EDITOR) && pheader.value1>=3) {
   if(!ReadData(&editor_start_hand,4) || editor_start_hand>254)return false;
   if(pheader.value1>=4 && (!ReadData(&editor_start_hand1,4) || editor_start_hand1>254))return false;
   for(int p=0;p<2;++p){DeckArray deck;for(auto* list:{&deck.main,&deck.extra}){uint32_t count=0;if(!ReadData(&count,4) || count>254)return false;list->resize(count);if(!ReadData(list->data(),count*4))return false;}decks.push_back(deck);}
  }
	}
	else {
		for (int p = 0; p < player_count; ++p) {
			DeckArray deck;
			uint32_t main = Read<uint32_t>();
			if (main > MAINC_MAX)
				return false;
			if (main)
				deck.main.resize(main);
			if (!ReadData(deck.main.data(), main * sizeof(uint32_t)))
				return false;
			uint32_t extra = Read<uint32_t>();
			if (extra > MAINC_MAX)
				return false;
			if (extra)
				deck.extra.resize(extra);
			if (!ReadData(deck.extra.data(), extra * sizeof(uint32_t)))
				return false;
			decks.push_back(deck);
		}
	}
	return true;
}

}

namespace ygo {
int Replay::ApplyEditorInput(intptr_t h,const editor_command& command){if(command.opcode!=11)return editor_apply(h,&command);if(pheader.value1<6 || command.value<1 || command.value>512)return EDITOR_BAD_COMMAND;std::vector<editor_command> group(command.value);for(auto& cmd:group){unsigned char response[SIZE_RETURN_VALUE]{};bool edit=false;if(!ReadNextInput(response,cmd,edit)||!edit||cmd.opcode==11)return EDITOR_BAD_COMMAND;}return editor_apply_group(h,group.data(),uint32_t(group.size()));}
}
