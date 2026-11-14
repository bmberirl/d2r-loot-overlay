#pragma once
// Stream tally: cumulative counts of orbs, runes and gems. Saved to disk on every
// change so it survives game restarts; cleared only by an explicit reset.

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace LootDrop {

struct TallyEntry {
	std::string kind;   // "orb" | "rune" | "gem"
	std::string name;   // display name
	std::string type;   // ItemTypes code (gems: gema..gemz, gemo)
	std::string type2;  // gems: gem0..gem4 grade, pgem, or empty
	int32_t     rune  = 0;
	uint32_t    count = 0;
};

class Tally {
public:
	void Load(const std::filesystem::path& file);  // missing file = fresh tally
	void Add(const std::string& code, const TallyEntry& meta);
	void Reset();

	auto Json() const -> std::string;
	auto Version() const -> uint64_t;
	auto Total() const -> uint64_t;

private:
	void SaveLocked() const;

	mutable std::mutex                mutex_;
	std::filesystem::path             file_;
	std::map<std::string, TallyEntry> entries_;  // by item code
	int64_t                           startedMs_ = 0;
	uint64_t                          version_   = 1;
};

// Category of an item for the tally, or "" when it is not tallied.
auto TallyKind(const std::string& code, const std::string& type, int32_t runeNumber) -> std::string;
auto NowEpochMs() -> int64_t;

}
