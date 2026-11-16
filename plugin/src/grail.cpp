#include "grail.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace LootDrop {

namespace {

constexpr uint64_t MaxFileBytes = 32ull * 1024 * 1024;
constexpr uint32_t MaxEntries   = 200000;

class Cursor {
public:
	explicit Cursor(const std::string& b) : b_(b) {}
	auto U16(uint16_t* v) -> bool { return Copy(v, 2); }
	auto U32(uint32_t* v) -> bool { return Copy(v, 4); }
	auto Skip(size_t n) -> bool {
		if (n > b_.size() - p_) return false;
		p_ += n;
		return true;
	}
	auto Str(size_t n, std::string* out) -> bool {
		if (n > b_.size() - p_) return false;
		out->assign(b_.data() + p_, n);
		p_ += n;
		return true;
	}
	auto Peek4() const -> std::string { return b_.size() - p_ >= 4 ? b_.substr(p_, 4) : std::string {}; }
	auto AtEnd() const -> bool { return p_ == b_.size(); }

private:
	auto Copy(void* v, size_t n) -> bool {
		if (n > b_.size() - p_) return false;
		std::memcpy(v, b_.data() + p_, n);
		p_ += n;
		return true;
	}
	const std::string& b_;
	size_t             p_ = 0;
};

auto ReadShared(const std::filesystem::path& file, std::string* out) -> bool {
	const HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size {};
	bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && static_cast<uint64_t>(size.QuadPart) <= MaxFileBytes;
	if (ok) {
		out->resize(static_cast<size_t>(size.QuadPart));
		DWORD got = 0;
		ok = out->empty() || (ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &got, nullptr) && got == out->size());
	}
	CloseHandle(h);
	return ok;
}

struct FileStamp {
	bool          exists = false;
	FILETIME      written {};
	uint64_t      size = 0;
};

auto Stamp(const std::filesystem::path& file) -> FileStamp {
	FileStamp s;
	WIN32_FILE_ATTRIBUTE_DATA a {};
	if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &a)) {
		s.exists  = true;
		s.written = a.ftLastWriteTime;
		s.size    = (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
	}
	return s;
}

}

auto ParseGrailFile(const std::string& bytes, std::vector<std::string>* found, std::string* error) -> bool {
	auto fail = [&](const char* why) {
		if (error) *error = why;
		return false;
	};
	Cursor c(bytes);
	std::string magic;
	uint32_t version = 0, stored = 0;
	if (!c.Str(4, &magic) || magic != "GRL1") return fail("not a Grail On! file (no GRL1 header)");
	if (!c.U32(&version) || version != 2) return fail("unknown Grail On! file version");
	if (!c.U32(&stored) || stored > MaxEntries) return fail("bad stored-copy count");
	// Stored copies: key, 2 flag bytes, seed, item code, five u32 (the last is the
	// length of the item bytes), then the item bytes.
	for (uint32_t i = 0; i < stored; ++i) {
		uint16_t keyLen = 0;
		uint32_t f[5] {};
		if (!c.U16(&keyLen) || !c.Skip(keyLen) || !c.Skip(2 + 4 + 4)) return fail("stored copies cut short");
		for (auto& v : f) {
			if (!c.U32(&v)) return fail("stored copies cut short");
		}
		if (f[4] > 4096 || !c.Skip(f[4])) return fail("bad item length in a stored copy");
	}
	uint32_t hashes = 0, keys = 0;
	if (!c.U32(&hashes) || hashes > MaxEntries || !c.Skip(4ull * hashes)) return fail("bad hash list");
	if (!c.U32(&keys) || keys > MaxEntries) return fail("bad found-list count");
	found->clear();
	found->reserve(keys);
	for (uint32_t i = 0; i < keys; ++i) {
		uint16_t keyLen = 0;
		std::string key;
		if (!c.U16(&keyLen) || !c.Str(keyLen, &key)) return fail("found list cut short");
		found->push_back(std::move(key));
	}
	// The next section is OUT1 (or the end of the file); anything else means the
	// layout is not the one this reader knows, so its counts cannot be trusted.
	if (!c.AtEnd() && c.Peek4() != "OUT1") return fail("unexpected data after the found list");
	return true;
}

void GrailReader::Configure(std::filesystem::path saveDir, std::string mode, uint32_t total, std::unordered_set<std::string> excluded) {
	dir_      = std::move(saveDir);
	mode_     = std::move(mode);
	total_    = total;
	excluded_ = std::move(excluded);
	lastSignature_.clear();
	progress_ = {};
	error_.clear();
}

auto GrailReader::Poll() -> bool {
	if (dir_.empty() || total_ == 0) return false;
	const auto hcPath = dir_ / L"grail-hardcore.dat";
	const auto scPath = dir_ / L"grail-softcore.dat";
	const FileStamp hc = Stamp(hcPath);
	const FileStamp sc = Stamp(scPath);
	bool hardcore = mode_ == "hardcore";
	if (mode_ == "auto") {
		if (hc.exists && sc.exists) hardcore = CompareFileTime(&hc.written, &sc.written) >= 0;
		else hardcore = hc.exists;
	}
	const FileStamp& s = hardcore ? hc : sc;
	const auto& path   = hardcore ? hcPath : scPath;

	wchar_t sig[96];
	swprintf_s(sig, L"%d:%d:%08lx%08lx:%llu", hardcore ? 1 : 0, s.exists ? 1 : 0, s.written.dwHighDateTime, s.written.dwLowDateTime, static_cast<unsigned long long>(s.size));
	if (lastSignature_ == sig) return false;

	GrailProgress next;
	next.mode  = hardcore ? "hardcore" : "softcore";
	next.total = total_;
	if (!s.exists) {
		error_ = "no Grail On! file at " + path.string();
		lastSignature_ = sig;
	} else {
		std::string bytes;
		std::vector<std::string> found;
		std::string why;
		if (!ReadShared(path, &bytes) || !ParseGrailFile(bytes, &found, &why)) {
			// Possibly caught mid-save: keep the last good progress and retry next poll.
			error_ = why.empty() ? "could not read " + path.string() : why;
			return false;
		}
		std::unordered_set<std::string> seen;
		for (const auto& k : found) {
			if ((k.rfind("u:", 0) == 0 || k.rfind("s:", 0) == 0) && !excluded_.count(k)) seen.insert(k);
		}
		next.ok    = true;
		next.found = std::min<uint32_t>(static_cast<uint32_t>(seen.size()), total_);
		error_.clear();
		lastSignature_ = sig;
	}
	if (next == progress_) return false;
	progress_ = next;
	return true;
}

}
