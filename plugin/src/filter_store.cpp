#include "filter_store.h"

#include <windows.h>

#include <cstdio>

namespace LootDrop {

auto FilterJsonAllowed(const std::string& json) -> bool {
	if (json.size() < 2 || json.size() > MaxFilterBytes || json.front() != '{' || json.back() != '}') return false;
	// Numbers, short ASCII words and JSON punctuation only: the filter holds item ids,
	// rune numbers, item codes and mode names, never free text.
	for (const unsigned char ch : json) {
		const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '"' ||
		                ch == ',' || ch == ':' || ch == ' ' || ch == '-' || ch == '_' || ch == '.';
		if (!ok) return false;
	}
	return true;
}

void FilterStore::Load(const std::filesystem::path& file) {
	std::lock_guard lock(mutex_);
	file_ = file;
	FILE* f = nullptr;
	if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || f == nullptr) return;
	std::string text;
	char buf[4096];
	size_t n = 0;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() <= MaxFilterBytes) text.append(buf, n);
	std::fclose(f);
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
	if (FilterJsonAllowed(text)) json_ = text;
}

auto FilterStore::Set(const std::string& json) -> bool {
	if (!FilterJsonAllowed(json)) return false;
	std::lock_guard lock(mutex_);
	if (json == json_) return true;
	json_ = json;
	++version_;
	if (!file_.empty()) {
		// Write a temp file, then swap it in, so a crash never leaves half a filter.
		const auto tmp = std::filesystem::path(file_).concat(L".tmp");
		FILE* f = nullptr;
		if (_wfopen_s(&f, tmp.c_str(), L"wb") == 0 && f != nullptr) {
			const bool ok = std::fwrite(json_.data(), 1, json_.size(), f) == json_.size();
			std::fclose(f);
			if (ok) MoveFileExW(tmp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		}
	}
	return true;
}

auto FilterStore::Json() const -> std::string {
	std::lock_guard lock(mutex_);
	return json_.empty() ? "null" : json_;
}

auto FilterStore::Version() const -> uint64_t {
	std::lock_guard lock(mutex_);
	return version_;
}

}
