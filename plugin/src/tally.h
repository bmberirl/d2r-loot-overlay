#pragma once
// Stream tally: cumulative counts of orbs, runes and gems. Saved to disk on every
// change so it survives game restarts; cleared only by an explicit reset.

#include "grail.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace LootDrop {

struct TallyEntry {
	std::string kind;   // "orb" | "rune" | "gem" | "charm"
	std::string name;   // display name
	std::string type;   // ItemTypes code (gems: gema..gemz, gemo)
	std::string type2;  // gems: gem0..gem4 grade, pgem, or empty
	int32_t     rune   = 0;
	int32_t     record = -1;  // charms: UniqueItems *ID
	uint32_t    count  = 0;
};

class Tally {
public:
	void Load(const std::filesystem::path& file);  // missing file = fresh tally
	void Add(const std::string& code, const TallyEntry& meta);
	void Reset();
	// Every unique charm the mod defines, as a JSON array of {"id":..,"name":..},
	// so the page can list charms not found yet.
	void SetCharmCatalog(std::string json);
	// Holy grail progress shown above the tally (from the Grail On! plugin's file).
	void SetGrail(const GrailProgress& grail);

	auto Json() const -> std::string;
	auto Version() const -> uint64_t;
	auto Total() const -> uint64_t;

private:
	void SaveLocked() const;

	mutable std::mutex                mutex_;
	std::filesystem::path             file_;
	std::map<std::string, TallyEntry> entries_;  // by item code
	std::string                       charmCatalog_ = "[]";
	GrailProgress                     grail_;
	int64_t                           startedMs_ = 0;
	uint64_t                          version_   = 1;
};

// Category of an item for the tally, or "" when it is not tallied.
auto TallyKind(const std::string& code, const std::string& type, int32_t runeNumber, const std::string& quality) -> std::string;
// Item types that are charms (small, large, grand, and Reimagined's Sunder charm).
auto IsCharmType(const std::string& type) -> bool;
// Tally key: unique charms are counted per unique record, everything else per item code.
auto TallyKey(const std::string& kind, const std::string& code, int32_t recordId) -> std::string;
auto NowEpochMs() -> int64_t;

}
