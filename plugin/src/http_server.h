#pragma once
// Minimal loopback-only HTTP server: serves the OBS overlay page and a
// Server-Sent Events stream of drops. No external dependencies.

#include "drop_feed.h"
#include "filter_store.h"
#include "tally.h"

#include <atomic>
#include <functional>
#include <cstdint>
#include <string>
#include <thread>

namespace LootDrop {

class HttpServer {
public:
	struct Pages {
		std::string overlay;  // GET /
		std::string tally;    // GET /tally
		std::string control;  // GET /control
		std::string filter;   // GET /filter
		std::string catalog;  // GET /catalog.json (uniques and set items that can drop)
	};
	HttpServer(DropFeed& feed, Tally& tally, FilterStore& filter, Pages pages, std::function<void()> onReset);
	~HttpServer();

	// Binds 127.0.0.1:port. Returns false (with error text) when the port is taken.
	auto Start(uint16_t port, std::string* error) -> bool;
	void Stop();

	auto Port() const -> uint16_t { return port_; }
	auto ClientCount() const -> uint32_t { return clients_.load(); }

private:
	void Run();

	DropFeed&             feed_;
	Tally&                tally_;
	FilterStore&          filter_;
	Pages                pages_;
	std::function<void()> onReset_;
	uint16_t              port_ = 0;
	uintptr_t             listen_ = ~uintptr_t { 0 };
	std::atomic<bool>     stop_ { false };
	std::atomic<uint32_t> clients_ { 0 };
	std::thread           thread_;
	bool                  wsaStarted_ = false;
};

}
