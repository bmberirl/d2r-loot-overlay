#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace LootDrop {

namespace {

auto ReadUInt(const wchar_t* section, const wchar_t* key, uint32_t fallback, const std::wstring& path) -> uint32_t {
	wchar_t buf[64] {};
	GetPrivateProfileStringW(section, key, L"", buf, 64, path.c_str());
	if (buf[0] == L'\0') {
		return fallback;
	}
	wchar_t* end = nullptr;
	const unsigned long value = wcstoul(buf, &end, 0);  // accepts 0x.. and decimal
	return end != buf ? static_cast<uint32_t>(value) : fallback;
}

auto ReadInt(const wchar_t* section, const wchar_t* key, int32_t fallback, const std::wstring& path) -> int32_t {
	wchar_t buf[64] {};
	GetPrivateProfileStringW(section, key, L"", buf, 64, path.c_str());
	if (buf[0] == L'\0') {
		return fallback;
	}
	wchar_t* end = nullptr;
	const long value = wcstol(buf, &end, 0);
	return end != buf ? static_cast<int32_t>(value) : fallback;
}

auto ReadString(const wchar_t* section, const wchar_t* key, const std::string& fallback, const std::wstring& path) -> std::string {
	wchar_t buf[512] {};
	GetPrivateProfileStringW(section, key, L"\x01", buf, 512, path.c_str());
	if (buf[0] == L'\x01') {
		return fallback;
	}
	char out[1024] {};
	WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
	return out;
}

constexpr const char* DefaultIni = R"(; Loot Drop Overlay settings. Lines starting with ; are comments.
; Restart the game after editing.

[server]
; The OBS overlay is served at http://127.0.0.1:<port>/ (this machine only).
port = 8766

[scanner]
; 0 turns ground-drop detection off (the overlay and lootdrop-test still work).
enabled = 1
; Scan the ground items every N rendered frames.
scan_every_frames = 6
; 1 = refuse to scan unless the native layout matches items the SDK reports.
require_layout_check = 1
; 1 = only announce items caught while falling (fresh drops). 0 = every item newly
; seen on the floor, including ones already there when you arrive.
dropping_only = 1

[grail]
; Holy grail progress on the /tally overlay, read from the Grail On! plugin's file.
enabled = 1
; auto = the grail saved last; or hardcore / softcore.
mode = auto
; 0 = every unique and set item that can drop in the mod. Any other number replaces it.
total = 0

[native]
; D2R 3.4.95032 native layout. Only change these if a game update broke detection.
unit_table_pattern =
; Or the table's address inside D2R.exe for this exact game build (hex RVA).
unit_table_rva = 0
unit_table_rip_offset = 3
unit_table_rip_end = 7
unit_table_extra = 0
buckets_per_type = 128
item_type_index = 4
unit_type = 0x00
unit_class_id = 0x04
unit_id = 0x08
unit_mode = 0x0C
unit_data = 0x10
unit_path = 0x38
unit_next_hash = 0x158
item_quality = 0x00
item_flags = 0x18
item_file_index = 0x34
path_x = 0x10
path_y = 0x14
mode_on_ground = 3
mode_dropping = 5
)";

}

void EnsureDefaultSettings(const std::filesystem::path& iniPath) {
	std::error_code ec;
	if (std::filesystem::exists(iniPath, ec)) {
		return;
	}
	std::filesystem::create_directories(iniPath.parent_path(), ec);
	FILE* f = nullptr;
	if (_wfopen_s(&f, iniPath.c_str(), L"wb") == 0 && f != nullptr) {
		std::fputs(DefaultIni, f);
		std::fclose(f);
	}
}

auto LoadSettings(const std::filesystem::path& iniPath) -> Settings {
	Settings s;
	const std::wstring p = iniPath.wstring();
	const uint32_t port  = ReadUInt(L"server", L"port", s.port, p);
	s.port               = (port >= 1024 && port <= 65535) ? static_cast<uint16_t>(port) : s.port;
	s.scannerEnabled     = ReadUInt(L"scanner", L"enabled", 1, p) != 0;
	s.scanEveryFrames    = std::max<uint32_t>(1, ReadUInt(L"scanner", L"scan_every_frames", s.scanEveryFrames, p));
	s.requireLayoutCheck = ReadUInt(L"scanner", L"require_layout_check", 1, p) != 0;
	s.droppingOnly       = ReadUInt(L"scanner", L"dropping_only", 1, p) != 0;
	s.grailEnabled       = ReadUInt(L"grail", L"enabled", 1, p) != 0;
	s.grailMode          = ReadString(L"grail", L"mode", s.grailMode, p);
	if (s.grailMode != "hardcore" && s.grailMode != "softcore") s.grailMode = "auto";
	s.grailTotal         = ReadUInt(L"grail", L"total", 0, p);
	s.layout.unitTableRva = ReadUInt(L"native", L"unit_table_rva", 0, p);

	auto& n              = s.layout;
	n.unitTablePattern   = ReadString(L"native", L"unit_table_pattern", n.unitTablePattern, p);
	n.unitTableRipOffset = ReadInt(L"native", L"unit_table_rip_offset", n.unitTableRipOffset, p);
	n.unitTableRipEnd    = ReadInt(L"native", L"unit_table_rip_end", n.unitTableRipEnd, p);
	n.unitTableExtra     = ReadInt(L"native", L"unit_table_extra", n.unitTableExtra, p);
	n.bucketsPerType     = ReadUInt(L"native", L"buckets_per_type", n.bucketsPerType, p);
	n.itemTypeIndex      = ReadUInt(L"native", L"item_type_index", n.itemTypeIndex, p);
	n.unitType           = ReadUInt(L"native", L"unit_type", n.unitType, p);
	n.unitClassId        = ReadUInt(L"native", L"unit_class_id", n.unitClassId, p);
	n.unitId             = ReadUInt(L"native", L"unit_id", n.unitId, p);
	n.unitMode           = ReadUInt(L"native", L"unit_mode", n.unitMode, p);
	n.unitData           = ReadUInt(L"native", L"unit_data", n.unitData, p);
	n.unitPath           = ReadUInt(L"native", L"unit_path", n.unitPath, p);
	n.unitNextHash       = ReadUInt(L"native", L"unit_next_hash", n.unitNextHash, p);
	n.itemQuality        = ReadUInt(L"native", L"item_quality", n.itemQuality, p);
	n.itemFlags          = ReadUInt(L"native", L"item_flags", n.itemFlags, p);
	n.itemFileIndex      = ReadUInt(L"native", L"item_file_index", n.itemFileIndex, p);
	n.pathX              = ReadUInt(L"native", L"path_x", n.pathX, p);
	n.pathY              = ReadUInt(L"native", L"path_y", n.pathY, p);
	n.modeOnGround       = ReadUInt(L"native", L"mode_on_ground", n.modeOnGround, p);
	n.modeDropping       = ReadUInt(L"native", L"mode_dropping", n.modeDropping, p);
	return s;
}

}
