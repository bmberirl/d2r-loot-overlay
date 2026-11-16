#pragma once
// Item names from the active mod's own TXT tables and string JSON files.
// Read once at load from disk, so no native table layouts are needed.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LootDrop {

// A unique or set item that can drop, for the overlay's filter page.
struct CatalogItem {
	char        kind;  // 'u' unique, 's' set item
	uint32_t    id;    // *ID, as reported in drop events
	std::string name;
	std::string base;
};

struct BaseItem {
	std::string code;     // trimmed, e.g. "cap", "r30"
	std::string nameKey;  // namestr
	std::string name;     // display name
	std::string type;     // ItemTypes code, e.g. "gemr", "rune", "elix"
	std::string type2;    // second type, e.g. "gem4" (perfect) for gems
};

// Rows read from the running game's compiled tables (original D2R or a mod
// without text files on disk).
struct GameItemRow {
	std::string code;
	uint16_t    type  = 0;  // ItemTypes row
	uint16_t    type2 = 0;  // 0 = none
};
struct GameRecordRow {
	uint32_t    id = 0;     // *ID
	std::string key;        // index, the string key of its name
	std::string code;       // base item code
	uint8_t     flags = 0;  // bit 0 spawnable, bit 2 disabled
};

class ItemTables {
public:
	// Builds the same tables from the running game. lookup turns a string key into
	// display text ("" when the game has no such key).
	auto LoadFromGame(const std::vector<std::string>& typeCodes, const std::vector<GameItemRow>& items, const std::vector<GameRecordRow>& uniques,
	                  const std::vector<GameRecordRow>& sets, const std::function<std::string(const std::string&)>& lookup) -> std::string;
	// True when the game has Reimagined's orbs.
	auto HasOrbs() const -> bool { return BaseByCode("ka3") != nullptr && BaseByCode("oos") != nullptr; }
	// modDirectory is PluginContext::modDirectory. Returns a one-line summary.
	auto Load(const std::filesystem::path& modDirectory) -> std::string;

	// classId -> base item. Two candidate orderings exist (with and without the
	// "Expansion" separator rows); Calibrate picks the one the game uses.
	auto Base(uint32_t classId) const -> const BaseItem*;
	auto BaseByCode(const std::string& code) const -> const BaseItem*;
	// Feed (classId, code) pairs observed through the SDK. Returns true once a
	// mapping is confirmed.
	auto Calibrate(uint32_t classId, const std::string& code) -> bool;
	auto Calibrated() const -> bool { return calibrated_; }
	auto CalibrationSummary() const -> std::string;

	auto UniqueName(uint32_t recordId) const -> std::string;
	// (UniqueItems *ID, display name) for every unique charm in the mod.
	auto UniqueCharms() const -> const std::vector<std::pair<uint32_t, std::string>>& { return uniqueCharms_; }
	auto SetName(uint32_t recordId) const -> std::string;
	// Unique and set items that can drop (enabled, spawnable, with a base item),
	// and the grail keys ("u:Name") of the enabled uniques and sets that cannot.
	auto GrailTotal() const -> uint32_t { return grailTotal_; }
	auto Catalog() const -> const std::vector<CatalogItem>& { return catalog_; }
	auto GrailExcluded() const -> const std::unordered_set<std::string>& { return grailExcluded_; }
	auto Loaded() const -> bool { return !withSeparators_.empty(); }

private:
	auto Lookup(const std::string& key) const -> std::string;

	std::vector<BaseItem>                        withSeparators_;
	std::vector<BaseItem>                        withoutSeparators_;
	int                                          choice_     = 1;  // 0 = with, 1 = without
	bool                                         calibrated_ = false;
	uint32_t                                     votes_[2]   = { 0, 0 };
	uint32_t                                     misses_[2]  = { 0, 0 };
	std::unordered_map<uint32_t, std::string>    uniques_;  // *ID -> index key
	std::unordered_map<uint32_t, std::string>    sets_;
	std::unordered_map<std::string, std::string> strings_;  // Key -> enUS
	std::vector<std::pair<uint32_t, std::string>> uniqueCharms_;
	uint32_t                                     grailTotal_ = 0;
	std::vector<CatalogItem>                     catalog_;
	std::unordered_set<std::string>              grailExcluded_;
};

auto TrimCode(const std::string& code) -> std::string;
auto RuneNumber(const std::string& code) -> int32_t;

}
