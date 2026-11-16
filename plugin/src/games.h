#pragma once
// One folder per game under the plugin's data folder (d2r, reimagined, ...). Each
// keeps its own drop filter, tally and drop log, plus the game's item list from the
// last time it ran, so /filter can edit a game that is not running.

#include "filter_store.h"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace LootDrop {

struct GameInfo {
	std::string id;    // folder name: lowercase letters, digits and dashes
	std::string name;  // shown on /filter, e.g. "D2R", "Reimagined"
	bool        orbs = false;
};

class Games {
public:
	void Init(const std::filesystem::path& dataDir, const GameInfo& current);
	auto Current() const -> GameInfo;
	auto CurrentDir() const -> std::filesystem::path;
	// Stores the running game's item list (JSON array) and whether it has orbs.
	void SetCatalog(const std::string& json, bool orbs);
	auto Catalog(const std::string& id) const -> std::string;  // "[]" when unknown
	auto ListJson() const -> std::string;
	// The filter of a game id ("" = the running game). nullptr for an unknown id.
	auto Filter(const std::string& id) -> FilterStore*;
	auto CurrentFilter() -> FilterStore&;

private:
	auto Known(const std::string& id) const -> bool;

	mutable std::mutex                                  mutex_;
	std::filesystem::path                               root_;
	GameInfo                                            current_;
	std::string                                         currentCatalog_ = "[]";
	std::map<std::string, std::unique_ptr<FilterStore>> filters_;
};

auto GameIdFromName(const std::string& name) -> std::string;
auto ValidGameId(const std::string& id) -> bool;

}
