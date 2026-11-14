#include "drop_feed.h"

#include <cstdio>

namespace LootDrop {

auto JsonEscape(const std::string& text) -> std::string {
	std::string out;
	out.reserve(text.size() + 8);
	for (const unsigned char c : text) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += static_cast<char>(c);  // UTF-8 passes through
			}
		}
	}
	return out;
}

auto ToJson(const DropEvent& e) -> std::string {
	char head[256];
	std::snprintf(head, sizeof(head), "{\"seq\":%llu,\"time\":%lld,\"unitId\":%u,\"classId\":%u,", static_cast<unsigned long long>(e.seq), static_cast<long long>(e.timeMs), e.unitId, e.classId);
	std::string json = head;
	json += "\"code\":\"" + JsonEscape(e.code) + "\",";
	json += "\"quality\":\"" + JsonEscape(e.quality) + "\",";
	json += "\"name\":\"" + JsonEscape(e.name) + "\",";
	json += "\"base\":\"" + JsonEscape(e.baseName) + "\",";
	char tail[256];
	std::snprintf(tail, sizeof(tail), "\"flags\":%u,\"ethereal\":%s,\"identified\":%s,\"socketed\":%s,\"runeword\":%s,\"rune\":%d,\"record\":%d,\"level\":%d,\"x\":%d,\"y\":%d,\"test\":%s}",
	              e.flags, e.ethereal ? "true" : "false", e.identified ? "true" : "false", e.socketed ? "true" : "false", e.runeword ? "true" : "false", e.runeNumber, e.recordId,
	              e.levelId, e.x, e.y, e.test ? "true" : "false");
	json += tail;
	return json;
}

void DropFeed::SetLogPath(std::filesystem::path path) {
	std::lock_guard lock(mutex_);
	logPath_ = std::move(path);
}

void DropFeed::Publish(DropEvent event) {
	std::string line;
	std::filesystem::path logPath;
	{
		std::lock_guard lock(mutex_);
		event.seq = nextSeq_++;
		ring_.push_back(event);
		while (ring_.size() > RingSize) {
			ring_.pop_front();
		}
		line    = ToJson(event);
		logPath = logPath_;
	}
	if (!logPath.empty()) {
		FILE* f = nullptr;
		if (_wfopen_s(&f, logPath.c_str(), L"ab") == 0 && f != nullptr) {
			line += '\n';
			std::fwrite(line.data(), 1, line.size(), f);
			std::fclose(f);
		}
	}
}

auto DropFeed::Recent(size_t max) const -> std::vector<DropEvent> {
	std::lock_guard lock(mutex_);
	const size_t start = ring_.size() > max ? ring_.size() - max : 0;
	return { ring_.begin() + static_cast<ptrdiff_t>(start), ring_.end() };
}

auto DropFeed::Since(uint64_t afterSeq) const -> std::vector<DropEvent> {
	std::lock_guard lock(mutex_);
	std::vector<DropEvent> out;
	for (const auto& e : ring_) {
		if (e.seq > afterSeq) {
			out.push_back(e);
		}
	}
	return out;
}

auto DropFeed::LastSeq() const -> uint64_t {
	std::lock_guard lock(mutex_);
	return nextSeq_ - 1;
}

auto DropFeed::Count() const -> uint64_t {
	return LastSeq();
}

}
