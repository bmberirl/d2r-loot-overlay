#include "ground_scanner.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace LootDrop {

namespace {

constexpr uint32_t UnitTypeItem = 4;
constexpr uint32_t MaxChain     = 4096;  // guard against a looped hash chain

// Copies n bytes from a native address; false instead of crashing on a bad pointer.
auto SafeCopy(void* dst, uintptr_t src, size_t n) noexcept -> bool {
	if (src < 0x10000 || src > 0x00007FFFFFFFFFFFULL) {
		return false;
	}
	__try {
		std::memcpy(dst, reinterpret_cast<const void*>(src), n);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

template <typename T>
auto Read(uintptr_t addr, T* out) noexcept -> bool {
	return SafeCopy(out, addr, sizeof(T));
}

auto ParsePattern(const std::string& text, std::vector<int>* bytes) -> bool {
	bytes->clear();
	for (size_t i = 0; i < text.size();) {
		if (text[i] == ' ') {
			++i;
			continue;
		}
		if (text[i] == '?') {
			bytes->push_back(-1);
			i += (i + 1 < text.size() && text[i + 1] == '?') ? 2 : 1;
			continue;
		}
		if (i + 1 >= text.size()) {
			return false;
		}
		char hex[3] = { text[i], text[i + 1], 0 };
		char* end   = nullptr;
		const long v = std::strtol(hex, &end, 16);
		if (end != hex + 2) {
			return false;
		}
		bytes->push_back(static_cast<int>(v));
		i += 2;
	}
	return !bytes->empty();
}

}

auto GroundScanner::Init(uintptr_t exeBase, const NativeLayout& layout, std::string* why) -> bool {
	layout_  = layout;
	exeBase_ = exeBase;
	ready_   = false;
	table_   = 0;

	if (layout.unitTableRva != 0) {
		table_ = exeBase + layout.unitTableRva;
		ready_ = true;
		return true;
	}
	if (layout.unitTablePattern.empty()) {
		autoLocate_ = true;
		if (why) *why = "no pattern or RVA configured; the table is located automatically in game";
		return false;
	}
	std::vector<int> pat;
	if (!ParsePattern(layout.unitTablePattern, &pat)) {
		if (why) *why = "unit_table_pattern is not valid hex";
		return false;
	}

	// Walk D2R.exe's executable sections (decrypted at runtime).
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
	const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exeBase + dos->e_lfanew);
	const auto* sec = IMAGE_FIRST_SECTION(nt);
	uintptr_t   match = 0;
	uint32_t    matches = 0;
	for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
		if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
			continue;
		}
		const auto* begin = reinterpret_cast<const uint8_t*>(exeBase + sec->VirtualAddress);
		const size_t size = sec->Misc.VirtualSize;
		for (size_t i = 0; i + pat.size() <= size; ++i) {
			size_t k = 0;
			for (; k < pat.size(); ++k) {
				if (pat[k] >= 0 && begin[i + k] != static_cast<uint8_t>(pat[k])) {
					break;
				}
			}
			if (k == pat.size()) {
				match = reinterpret_cast<uintptr_t>(begin + i);
				if (++matches > 1) {
					break;
				}
			}
		}
	}
	if (matches != 1) {
		if (why) *why = matches == 0 ? "unit table pattern not found (game updated?)" : "unit table pattern matched more than once; refusing to guess";
		return false;
	}
	int32_t rel = 0;
	if (!Read(match + static_cast<uint32_t>(layout.unitTableRipOffset), &rel)) {
		if (why) *why = "unit table pattern unreadable";
		return false;
	}
	table_ = match + static_cast<uint32_t>(layout.unitTableRipEnd) + rel + layout.unitTableExtra;
	ready_ = true;
	return true;
}

auto GroundScanner::AutoLocate(const std::vector<KnownItem>& known, std::string* why) -> bool {
	ready_ = validated_ = false;
	if (known.size() < 3) {
		if (why) *why = "auto-locate needs at least 3 items on the character";
		return false;
	}
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase_);
	const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exeBase_ + dos->e_lfanew);
	const auto* sec = IMAGE_FIRST_SECTION(nt);
	const uint32_t perType   = layout_.bucketsPerType;
	const size_t   tableQ    = static_cast<size_t>(perType) * 6;  // six unit types
	const size_t   itemFirst = static_cast<size_t>(layout_.itemTypeIndex) * perType;

	auto plausible = [](uint64_t p) { return p == 0 || (p >= 0x10000 && p <= 0x00007FFFFFFFFFFFULL && (p & 7) == 0); };

	uint32_t candidates = 0;
	std::vector<uintptr_t> winners;
	std::vector<uint64_t>  buf;
	for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
		const DWORD ch = sec->Characteristics;
		if ((ch & IMAGE_SCN_MEM_WRITE) == 0 || (ch & IMAGE_SCN_MEM_EXECUTE) != 0) {
			continue;
		}
		const uintptr_t start = exeBase_ + sec->VirtualAddress;
		const size_t    count = sec->Misc.VirtualSize / sizeof(uint64_t);
		buf.assign(count, 0);
		if (!SafeCopy(buf.data(), start, count * sizeof(uint64_t))) {
			// Copy page by page and leave unreadable pages zero.
			for (size_t off = 0; off < count * sizeof(uint64_t); off += 4096) {
				SafeCopy(reinterpret_cast<uint8_t*>(buf.data()) + off, start + off, std::min<size_t>(4096, count * sizeof(uint64_t) - off));
			}
		}
		for (size_t i = 0; i + tableQ <= count; ++i) {
			// Cheap structural filter on the copy: every slot is null or a plausible pointer,
			// the player block has at least one head (D2R 3.4 keeps ~120 type-0 units there) and the item block at least 3.
			uint32_t players = 0, items = 0;
			bool     ok      = true;
			for (size_t q = 0; q < perType && ok; ++q) {
				const uint64_t v = buf[i + q];
				ok               = plausible(v);
				players += v != 0;
			}
			if (!ok || players == 0) {
				continue;
			}
			for (size_t q = 0; q < tableQ && ok; ++q) {
				ok = plausible(buf[i + q]);
				if (q >= itemFirst && q < itemFirst + perType) {
					items += buf[i + q] != 0;
				}
			}
			if (!ok || items < 3) {
				continue;
			}
			// Deref: player heads are type 0, item heads are type 4.
			++candidates;
			for (size_t q = 0; q < perType && ok; ++q) {
				uint32_t type = 99;
				ok = buf[i + q] == 0 || (Read(buf[i + q] + layout_.unitType, &type) && type == 0);
			}
			for (size_t q = itemFirst; q < itemFirst + perType && ok; ++q) {
				uint32_t type = 99;
				ok = buf[i + q] == 0 || (Read(buf[i + q] + layout_.unitType, &type) && type == UnitTypeItem);
			}
			if (ok) {
				winners.push_back(start + i * sizeof(uint64_t));
			}
		}
	}

	// Of the shape matches, keep the one that actually holds the character's items.
	for (const uintptr_t candidate : winners) {
		table_ = candidate;
		ready_ = true;
		std::string detail;
		if (Validate(known, &detail)) {
			char msg[300];
			std::snprintf(msg, sizeof(msg), "unit table found at D2R.exe+0x%llX (%u shape candidates, %zu matches); %s", static_cast<unsigned long long>(candidate - exeBase_), candidates,
			              winners.size(), detail.c_str());
			if (why) *why = msg;
			return true;
		}
	}
	ready_ = false;
	table_ = 0;
	char msg[200];
	std::snprintf(msg, sizeof(msg), "unit table not found: %u shape candidates, %zu with correct unit types, none held the inventory items", candidates, winners.size());
	if (why) *why = msg;
	return false;
}

auto GroundScanner::ReadItem(uintptr_t unit, GroundItem* out) -> bool {
	const auto& L = layout_;
	uint32_t type = 0;
	if (!Read(unit + L.unitType, &type) || type != UnitTypeItem) {
		return false;
	}
	uintptr_t data = 0, path = 0;
	bool ok = Read(unit + L.unitClassId, &out->classId) && Read(unit + L.unitId, &out->unitId) && Read(unit + L.unitMode, &out->mode) && Read(unit + L.unitData, &data);
	if (!ok || data == 0) {
		++faults_;
		return false;
	}
	ok = Read(data + L.itemQuality, &out->quality) && Read(data + L.itemFlags, &out->flags) && Read(data + L.itemFileIndex, &out->fileIndex);
	if (!ok) {
		++faults_;
		return false;
	}
	out->x = out->y = 0;
	if (Read(unit + L.unitPath, &path) && path != 0) {
		uint32_t x = 0, y = 0;
		if (Read(path + L.pathX, &x) && Read(path + L.pathY, &y)) {
			out->x = static_cast<int32_t>(x);
			out->y = static_cast<int32_t>(y);
		}
	}
	return true;
}

auto GroundScanner::All() -> std::vector<GroundItem> {
	std::vector<GroundItem> out;
	if (!ready_) {
		return out;
	}
	const uintptr_t buckets = table_ + static_cast<uintptr_t>(layout_.itemTypeIndex) * layout_.bucketsPerType * sizeof(uintptr_t);
	for (uint32_t b = 0; b < layout_.bucketsPerType; ++b) {
		uintptr_t unit = 0;
		if (!Read(buckets + b * sizeof(uintptr_t), &unit)) {
			++faults_;
			break;
		}
		for (uint32_t guard = 0; unit != 0 && guard < MaxChain; ++guard) {
			GroundItem item;
			if (ReadItem(unit, &item)) {
				out.push_back(item);
			}
			if (!Read(unit + layout_.unitNextHash, &unit)) {
				++faults_;
				break;
			}
		}
	}
	lastCount_ = static_cast<uint32_t>(out.size());
	return out;
}

auto GroundScanner::Validate(const std::vector<KnownItem>& known, std::string* why) -> bool {
	validated_ = false;
	if (!ready_) {
		if (why) *why = "unit table not resolved";
		return false;
	}
	if (known.empty()) {
		if (why) *why = "no inventory items to check against yet";
		return false;
	}
	const auto all = All();
	uint32_t found = 0, mismatched = 0;
	for (const auto& k : known) {
		for (const auto& u : all) {
			if (u.unitId == k.unitId) {
				(u.classId == k.classId && u.quality == k.quality) ? ++found : ++mismatched;
				break;
			}
		}
	}
	char buf[200];
	std::snprintf(buf, sizeof(buf), "layout check: %u/%zu inventory items matched, %u mismatched, %zu item units in table", found, known.size(), mismatched, all.size());
	if (why) *why = buf;
	// Need most of the sample and no contradictions.
	validated_ = mismatched == 0 && found >= 3 && found * 2 >= known.size();
	return validated_;
}

auto GroundScanner::Scan(bool droppingOnly) -> std::vector<GroundItem> {
	std::vector<GroundItem> fresh;
	if (!Ready()) {
		return fresh;
	}
	for (const auto& item : All()) {
		const bool dropping = item.mode == layout_.modeDropping;
		const bool onGround = item.mode == layout_.modeOnGround;
		if (!dropping && !onGround) {
			continue;
		}
		if (seen_.count(item.unitId) != 0) {
			continue;
		}
		seen_.insert(item.unitId);
		if (droppingOnly && !dropping) {
			continue;  // was already lying there when we first saw it
		}
		fresh.push_back(item);
	}
	return fresh;
}

}
