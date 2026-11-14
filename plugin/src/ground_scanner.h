#pragma once
// Reads D2R's client unit table to find items lying on, or falling to, the
// ground. Read-only: no hooks, no writes. Every native read is guarded, and the
// scanner refuses to run until its layout matches items the SDK reports.

#include "settings.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace LootDrop {

struct GroundItem {
	uint32_t unitId    = 0;
	uint32_t classId   = 0;
	uint32_t mode      = 0;
	uint32_t quality   = 0;
	uint32_t flags     = 0;
	uint32_t fileIndex = 0;
	int32_t  x         = 0;
	int32_t  y         = 0;
};

struct KnownItem {  // from InventoryService::forEachInventoryItem
	uint32_t unitId;
	uint32_t classId;
	uint32_t quality;
};

class GroundScanner {
public:
	// Resolves the table from the RVA or pattern. Returns false with a reason.
	// With neither configured it does nothing and AutoLocate must be used.
	auto Init(uintptr_t exeBase, const NativeLayout& layout, std::string* why) -> bool;
	// Finds the table by its shape in D2R.exe's writable data, then keeps the
	// candidate whose buckets contain the given inventory unit ids. In game only.
	auto AutoLocate(const std::vector<KnownItem>& known, std::string* why) -> bool;
	auto UsesAutoLocate() const -> bool { return autoLocate_; }
	// Confirms the layout: each known item must be found with the same class and quality.
	auto Validate(const std::vector<KnownItem>& known, std::string* why) -> bool;
	auto Ready() const -> bool { return ready_ && validated_; }
	auto Resolved() const -> bool { return ready_; }
	void ForceValidated() { validated_ = ready_; }

	// New ground items since the last call (each unitId reported once per game).
	auto Scan(bool droppingOnly) -> std::vector<GroundItem>;
	void ResetSeen() { seen_.clear(); }

	auto TableAddress() const -> uintptr_t { return table_; }
	auto Faults() const -> uint64_t { return faults_; }
	auto LastCount() const -> uint32_t { return lastCount_; }

	// Diagnostic: every item unit in the table (any mode).
	auto All() -> std::vector<GroundItem>;

private:
	auto ReadItem(uintptr_t unit, GroundItem* out) -> bool;

	NativeLayout                 layout_;
	uintptr_t                    exeBase_   = 0;
	uintptr_t                    table_     = 0;
	bool                         ready_     = false;
	bool                         validated_ = false;
	bool                         autoLocate_ = false;
	uint64_t                     faults_    = 0;
	uint32_t                     lastCount_ = 0;
	std::unordered_set<uint32_t> seen_;
};

}
