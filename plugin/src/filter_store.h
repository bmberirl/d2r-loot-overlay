#pragma once
// The drop overlay's saved filter, chosen on the /filter page. The server stores it
// as an opaque JSON object (the overlay page applies it), after checking its size
// and that it only uses the characters such an object needs.

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace LootDrop {

class FilterStore {
public:
	void Load(const std::filesystem::path& file);  // missing or invalid file = no filter
	auto Set(const std::string& json) -> bool;     // false when the JSON is refused
	auto Json() const -> std::string;              // "null" when nothing is saved
	auto Version() const -> uint64_t;

private:
	mutable std::mutex    mutex_;
	std::filesystem::path file_;
	std::string           json_;
	uint64_t              version_ = 1;
};

constexpr size_t MaxFilterBytes = 64 * 1024;
auto FilterJsonAllowed(const std::string& json) -> bool;

}
