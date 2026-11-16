#include "games.h"

#include "drop_feed.h"  // JsonEscape

#include <windows.h>

#include <cctype>
#include <cstdio>

namespace LootDrop {

namespace {

auto ReadSmallFile(const std::filesystem::path& file, size_t cap) -> std::string {
	FILE* f = nullptr;
	if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || f == nullptr) return {};
	std::string text;
	char buf[8192];
	size_t n = 0;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
		text.append(buf, n);
		if (text.size() > cap) {
			text.clear();
			break;
		}
	}
	std::fclose(f);
	return text;
}

void WriteFileAtomic(const std::filesystem::path& file, const std::string& text) {
	const auto tmp = std::filesystem::path(file).concat(L".tmp");
	FILE* f = nullptr;
	if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || f == nullptr) return;
	const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
	std::fclose(f);
	if (ok) MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

// game.txt: line 1 the display name, line 2 "orbs" or "no-orbs".
auto ReadInfo(const std::filesystem::path& dir, const std::string& id) -> GameInfo {
	GameInfo info { id, id, false };
	const std::string text = ReadSmallFile(dir / L"game.txt", 4096);
	const size_t nl = text.find('\n');
	std::string name = text.substr(0, nl);
	while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
	if (!name.empty()) info.name = name;
	info.orbs = nl != std::string::npos && text.compare(nl + 1, 4, "orbs") == 0;
	return info;
}

constexpr size_t MaxCatalogBytes = 4 * 1024 * 1024;

}

auto GameIdFromName(const std::string& name) -> std::string {
	std::string id;
	for (const unsigned char c : name) {
		if (std::isalnum(c)) id += static_cast<char>(std::tolower(c));
		else if ((c == ' ' || c == '-' || c == '_') && !id.empty() && id.back() != '-') id += '-';
	}
	while (!id.empty() && id.back() == '-') id.pop_back();
	return id.empty() || id.size() > 48 ? "mod" : id;
}

auto ValidGameId(const std::string& id) -> bool {
	if (id.empty() || id.size() > 48) return false;
	for (const unsigned char c : id) {
		if (!(std::islower(c) || std::isdigit(c) || c == '-')) return false;
	}
	return true;
}

void Games::Init(const std::filesystem::path& dataDir, const GameInfo& current) {
	std::lock_guard lock(mutex_);
	root_    = dataDir;
	current_ = current;
	std::error_code ec;
	std::filesystem::create_directories(root_ / current_.id, ec);
	WriteFileAtomic(root_ / current_.id / L"game.txt", current_.name + "\n" + (current_.orbs ? "orbs" : "no-orbs") + "\n");
	auto store = std::make_unique<FilterStore>();
	store->Load(root_ / current_.id / L"drop-filter.json");
	filters_[current_.id] = std::move(store);
}

auto Games::Current() const -> GameInfo {
	std::lock_guard lock(mutex_);
	return current_;
}

auto Games::CurrentDir() const -> std::filesystem::path {
	std::lock_guard lock(mutex_);
	return root_ / current_.id;
}

void Games::SetCatalog(const std::string& json, bool orbs) {
	std::lock_guard lock(mutex_);
	currentCatalog_ = json;
	current_.orbs   = orbs;
	WriteFileAtomic(root_ / current_.id / L"catalog.json", json);
	WriteFileAtomic(root_ / current_.id / L"game.txt", current_.name + "\n" + (orbs ? "orbs" : "no-orbs") + "\n");
}

auto Games::Known(const std::string& id) const -> bool {
	std::error_code ec;
	return ValidGameId(id) && (id == current_.id || std::filesystem::exists(root_ / id / L"catalog.json", ec));
}

auto Games::Catalog(const std::string& id) const -> std::string {
	std::lock_guard lock(mutex_);
	if (id.empty() || id == current_.id) return currentCatalog_;
	if (!Known(id)) return "[]";
	std::string text = ReadSmallFile(root_ / id / L"catalog.json", MaxCatalogBytes);
	return !text.empty() && text.front() == '[' ? text : "[]";
}

auto Games::ListJson() const -> std::string {
	std::lock_guard lock(mutex_);
	std::string json = "{\"current\":\"" + current_.id + "\",\"games\":[";
	json += "{\"id\":\"" + current_.id + "\",\"name\":\"" + JsonEscape(current_.name) + "\",\"orbs\":" + (current_.orbs ? "true" : "false") + "}";
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
		const std::string id = entry.path().filename().string();
		if (!entry.is_directory(ec) || id == current_.id || !Known(id)) continue;
		const GameInfo info = ReadInfo(entry.path(), id);
		json += ",{\"id\":\"" + id + "\",\"name\":\"" + JsonEscape(info.name) + "\",\"orbs\":" + (info.orbs ? "true" : "false") + "}";
	}
	return json + "]}";
}

auto Games::Filter(const std::string& id) -> FilterStore* {
	std::lock_guard lock(mutex_);
	const std::string key = id.empty() ? current_.id : id;
	if (const auto it = filters_.find(key); it != filters_.end()) return it->second.get();
	if (!Known(key)) return nullptr;
	auto store = std::make_unique<FilterStore>();
	store->Load(root_ / key / L"drop-filter.json");
	return (filters_[key] = std::move(store)).get();
}

auto Games::CurrentFilter() -> FilterStore& {
	return *Filter("");
}

}
