#pragma once
// Holy grail progress read from the Grail On! plugin's own storage file
// (<Saved Games>\Diablo II Resurrected\mods\<mod>\grail-hardcore.dat and
// grail-softcore.dat). Read-only: the file is opened with full sharing and never
// written, so Grail On! can save at any time.
//
// Counted: unique and set items found ("u:" and "s:" entries of the file's found
// list) against every unique and set item of the mod that can drop. Ethereal
// copies and runewords, which Grail On!'s own overview can also count, are not.

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace LootDrop {

struct GrailProgress {
	bool        ok    = false;
	uint32_t    found = 0;
	uint32_t    total = 0;
	std::string mode;  // "hardcore" | "softcore"

	auto operator==(const GrailProgress&) const -> bool = default;
};

// Parses a Grail On! storage file (format GRL1 version 2) and returns its found
// list, e.g. "u:Goreshovel", "s:Angelic Halo". False when the layout is not the
// known one.
auto ParseGrailFile(const std::string& bytes, std::vector<std::string>* found, std::string* error) -> bool;

class GrailReader {
public:
	// mode: "auto" (the file written last), "hardcore" or "softcore".
	// excluded: found-list keys that are not part of the total (items that cannot drop).
	void Configure(std::filesystem::path saveDir, std::string mode, uint32_t total, std::unordered_set<std::string> excluded);
	// Re-reads the file when it changed. Returns true when the progress changed.
	auto Poll() -> bool;
	auto Progress() const -> const GrailProgress& { return progress_; }
	auto LastError() const -> const std::string& { return error_; }

private:
	std::filesystem::path           dir_;
	std::string                     mode_ = "auto";
	uint32_t                        total_ = 0;
	std::unordered_set<std::string> excluded_;
	std::wstring                    lastSignature_;
	GrailProgress                   progress_;
	std::string                     error_;
};

}
