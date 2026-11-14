#pragma once
// Item names from the active mod's own TXT tables and string JSON files.
// Read once at load from disk, so no native table layouts are needed.

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace LootDrop {

struct BaseItem {
	std::string code;     // trimmed, e.g. "cap", "r30"
	std::string nameKey;  // namestr
	std::string name;     // display name
	std::string type;     // ItemTypes code, e.g. "gemr", "rune", "elix"
	std::string type2;    // second type, e.g. "gem4" (perfect) for gems
};

class ItemTables {
public:
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
};

auto TrimCode(const std::string& code) -> std::string;
auto RuneNumber(const std::string& code) -> int32_t;

}
