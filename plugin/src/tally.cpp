#include "tally.h"

#include "drop_feed.h"  // JsonEscape

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace LootDrop {

namespace {

// Reimagined's seven orbs and their stack items (misc.txt codes).
const std::set<std::string> OrbCodes = { "ka3", "oos", "ooc", "ooa", "ooi", "ooe", "oor", "1ka", "1os", "1oc", "1oa", "1oi", "1oe", "1or" };

// Minimal reader for the file this class writes: one entry per line.
auto Field(const std::string& line, const char* key) -> std::string {
	const std::string needle = std::string("\"") + key + "\":";
	const size_t      at     = line.find(needle);
	if (at == std::string::npos) return {};
	size_t i = at + needle.size();
	if (i < line.size() && line[i] == '"') {
		std::string out;
		for (++i; i < line.size() && line[i] != '"'; ++i) {
			if (line[i] == '\\' && i + 1 < line.size()) ++i;
			out += line[i];
		}
		return out;
	}
	const size_t end = line.find_first_of(",}", i);
	return line.substr(i, end == std::string::npos ? std::string::npos : end - i);
}

auto EntryJson(const std::string& code, const TallyEntry& e) -> std::string {
	char nums[96];
	std::snprintf(nums, sizeof(nums), "\"rune\":%d,\"count\":%u}", e.rune, e.count);
	return "{\"code\":\"" + JsonEscape(code) + "\",\"kind\":\"" + JsonEscape(e.kind) + "\",\"name\":\"" + JsonEscape(e.name) + "\",\"type\":\"" + JsonEscape(e.type) + "\",\"type2\":\""
	     + JsonEscape(e.type2) + "\"," + nums;
}

}

auto NowEpochMs() -> int64_t {
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<int64_t>((t - 116444736000000000ULL) / 10000ULL);
}

auto TallyKind(const std::string& code, const std::string& type, int32_t runeNumber) -> std::string {
	if (runeNumber > 0) return "rune";
	if (OrbCodes.count(code) != 0) return "orb";
	// gema gemd geme gemr gems gemt gemz gemo, and gem stacks (gemx).
	if (type.size() == 4 && type.compare(0, 3, "gem") == 0) return "gem";
	return {};
}

void Tally::Load(const std::filesystem::path& file) {
	std::lock_guard lock(mutex_);
	file_ = file;
	entries_.clear();
	startedMs_ = NowEpochMs();
	std::ifstream in(file, std::ios::binary);
	std::string   line;
	while (std::getline(in, line)) {
		if (line.find("\"started\":") != std::string::npos) {
			startedMs_ = std::strtoll(Field(line, "started").c_str(), nullptr, 10);
		} else if (line.find("\"code\":") != std::string::npos) {
			TallyEntry e;
			e.kind  = Field(line, "kind");
			e.name  = Field(line, "name");
			e.type  = Field(line, "type");
			e.type2 = Field(line, "type2");
			e.rune  = static_cast<int32_t>(std::strtol(Field(line, "rune").c_str(), nullptr, 10));
			e.count = static_cast<uint32_t>(std::strtoul(Field(line, "count").c_str(), nullptr, 10));
			const std::string code = Field(line, "code");
			if (!code.empty() && e.count > 0) entries_[code] = e;
		}
	}
	++version_;
}

void Tally::Add(const std::string& code, const TallyEntry& meta) {
	std::lock_guard lock(mutex_);
	auto& e = entries_[code];
	const uint32_t count = e.count;
	e       = meta;
	e.count = count + 1;
	++version_;
	SaveLocked();
}

void Tally::Reset() {
	std::lock_guard lock(mutex_);
	entries_.clear();
	startedMs_ = NowEpochMs();
	++version_;
	SaveLocked();
}

auto Tally::Json() const -> std::string {
	std::lock_guard lock(mutex_);
	std::string json = "{\"started\":" + std::to_string(startedMs_) + ",\"version\":" + std::to_string(version_) + ",\"items\":[";
	bool first = true;
	for (const auto& [code, e] : entries_) {
		json += first ? "" : ",";
		json += EntryJson(code, e);
		first = false;
	}
	return json + "]}";
}

auto Tally::Version() const -> uint64_t {
	std::lock_guard lock(mutex_);
	return version_;
}

auto Tally::Total() const -> uint64_t {
	std::lock_guard lock(mutex_);
	uint64_t total = 0;
	for (const auto& [code, e] : entries_) total += e.count;
	return total;
}

void Tally::SaveLocked() const {
	if (file_.empty()) return;
	// Write a temp file, then swap it in, so a crash never leaves a half-written tally.
	std::string text = "{\n\"started\":" + std::to_string(startedMs_) + ",\n\"items\":[\n";
	bool first = true;
	for (const auto& [code, e] : entries_) {
		text += first ? "" : ",\n";
		text += EntryJson(code, e);
		first = false;
	}
	text += "\n]}\n";
	const auto tmp = std::filesystem::path(file_).concat(L".tmp");
	FILE* f = nullptr;
	if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || f == nullptr) return;
	std::fwrite(text.data(), 1, text.size(), f);
	std::fclose(f);
	MoveFileExW(tmp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

}
