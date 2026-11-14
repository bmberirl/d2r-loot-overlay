#pragma once
// A drop event and the feed that fans it out to the JSONL log and to HTTP clients.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace LootDrop {

struct DropEvent {
	uint64_t    seq        = 0;  // assigned by the feed
	int64_t     timeMs     = 0;  // Unix epoch milliseconds
	uint32_t    unitId     = 0;
	uint32_t    classId    = 0;
	std::string code;            // 4-char item code, trimmed ("r30", "cap")
	std::string quality;         // inferior|normal|superior|magic|set|rare|unique|crafted|unknown
	std::string name;            // display name (unique/set name, or base name)
	std::string baseName;        // base item display name
	uint32_t    flags      = 0;  // native item flags
	bool        ethereal   = false;
	bool        identified = false;
	bool        socketed   = false;
	bool        runeword   = false;
	int32_t     runeNumber = 0;  // 1..33 for El..Zod, else 0
	int32_t     recordId   = -1; // UniqueItems / SetItems *ID for unique and set items
	int32_t     levelId    = -1;
	int32_t     x          = 0;
	int32_t     y          = 0;
	bool        test       = false;
};

auto ToJson(const DropEvent& event) -> std::string;
auto JsonEscape(const std::string& text) -> std::string;

class DropFeed {
public:
	void SetLogPath(std::filesystem::path path);
	void Publish(DropEvent event);  // any thread

	// Snapshot of the newest events (oldest first), for replay to new clients.
	auto Recent(size_t max) const -> std::vector<DropEvent>;
	// Events with seq > afterSeq, oldest first.
	auto Since(uint64_t afterSeq) const -> std::vector<DropEvent>;
	auto LastSeq() const -> uint64_t;
	auto Count() const -> uint64_t;

private:
	mutable std::mutex      mutex_;
	std::deque<DropEvent>   ring_;
	uint64_t                nextSeq_ = 1;
	std::filesystem::path   logPath_;
	static constexpr size_t RingSize = 200;
};

}
