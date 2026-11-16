#pragma once
// Settings come from loot-drop-overlay.ini (Win32 INI). Native offsets live here
// so a game update can be handled by editing the file instead of rebuilding.

#include <cstdint>
#include <filesystem>
#include <string>

namespace LootDrop {

struct NativeLayout {
	// Pattern for the instruction that loads the client unit hash table address.
	// Hex bytes, "??" for wildcards. ripOffset is where the rel32 starts inside the
	// match and ripEnd is the instruction end used as the RIP base.
	std::string unitTablePattern;
	// Alternative to the pattern: the table's RVA inside D2R.exe for one exact build.
	uint32_t    unitTableRva       = 0;
	int32_t     unitTableRipOffset = 3;
	int32_t     unitTableRipEnd    = 7;
	int32_t     unitTableExtra     = 0;     // added after resolving (e.g. type * 0x400)
	uint32_t    bucketsPerType     = 128;
	uint32_t    itemTypeIndex      = 4;

	uint32_t unitType      = 0x00;
	uint32_t unitClassId   = 0x04;
	uint32_t unitId        = 0x08;
	uint32_t unitMode      = 0x0C;
	uint32_t unitData      = 0x10;
	uint32_t unitPath      = 0x38;
	uint32_t unitNextHash  = 0x158;

	uint32_t itemQuality   = 0x00;
	uint32_t itemFlags     = 0x18;
	uint32_t itemFileIndex = 0x34;

	uint32_t pathX         = 0x10;
	uint32_t pathY         = 0x14;

	uint32_t modeOnGround  = 3;
	uint32_t modeDropping  = 5;
};

struct Settings {
	uint16_t     port            = 8766;
	bool         scannerEnabled  = true;
	uint32_t     scanEveryFrames = 6;     // ~10 scans per second at 60 fps
	bool         requireLayoutCheck = true;
	// true: only items seen mid-drop (mode dropping) count, so items already lying
	// on the floor when you enter an area are not announced.
	bool         droppingOnly    = true;
	// Holy grail progress from the Grail On! plugin's file.
	bool         grailEnabled    = true;
	std::string  grailMode       = "auto";  // auto | hardcore | softcore
	uint32_t     grailTotal      = 0;       // 0 = count from the mod's tables
	NativeLayout layout;
};

auto LoadSettings(const std::filesystem::path& iniPath) -> Settings;
// Writes a commented default file when none exists. Never overwrites.
void EnsureDefaultSettings(const std::filesystem::path& iniPath);

}
