#include "http_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace LootDrop {

namespace {

constexpr size_t   MaxRequestBytes = 8 * 1024;
constexpr size_t   MaxClients      = 16;
constexpr uint32_t ReplayMax       = 50;
constexpr auto     KeepAlive       = std::chrono::seconds(15);
constexpr auto     ReadDeadline    = std::chrono::seconds(5);

struct Client {
	SOCKET                                sock = INVALID_SOCKET;
	std::string                           request;
	std::string                           pendingOut;
	bool                                  sse       = false;
	bool                                  closeWhenFlushed = false;
	uint64_t                              lastSeq   = 0;
	uint64_t                              lastTally = 0;
	std::chrono::steady_clock::time_point opened;
	std::chrono::steady_clock::time_point lastWrite;
};

void SetNonBlocking(SOCKET s) {
	u_long mode = 1;
	ioctlsocket(s, FIONBIO, &mode);
}

auto Lower(std::string s) -> std::string {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

auto HeaderValue(const std::string& request, const char* name) -> std::string {
	const std::string lower  = Lower(request);
	const std::string needle = std::string("\r\n") + name + ":";
	const size_t      at     = lower.find(needle);
	if (at == std::string::npos) {
		return {};
	}
	size_t start = at + needle.size();
	while (start < request.size() && request[start] == ' ') {
		++start;
	}
	const size_t end = request.find("\r\n", start);
	return request.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Only loopback hosts are accepted. This blocks DNS-rebinding pages from reading the feed.
auto HostAllowed(const std::string& host, uint16_t port) -> bool {
	const std::string h = Lower(host);
	const std::string p = ":" + std::to_string(port);
	return h == "127.0.0.1" || h == "localhost" || h == "127.0.0.1" + p || h == "localhost" + p;
}

auto QueryParam(const std::string& target, const char* key) -> std::string {
	const size_t q = target.find('?');
	if (q == std::string::npos) {
		return {};
	}
	const std::string query = "&" + target.substr(q + 1);
	const std::string needle = std::string("&") + key + "=";
	const size_t at = query.find(needle);
	if (at == std::string::npos) {
		return {};
	}
	const size_t start = at + needle.size();
	const size_t end   = query.find('&', start);
	return query.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

auto Response(int code, const char* reason, const char* type, const std::string& body, const char* extra = "") -> std::string {
	char head[512];
	std::snprintf(head, sizeof(head),
	              "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n%s\r\n", code, reason,
	              type, body.size(), extra);
	return std::string(head) + body;
}

auto SseEvent(const DropEvent& e) -> std::string {
	return "id: " + std::to_string(e.seq) + "\nevent: drop\ndata: " + ToJson(e) + "\n\n";
}

}

HttpServer::HttpServer(DropFeed& feed, Tally& tally, Pages pages, std::function<void()> onReset)
    : feed_(feed), tally_(tally), pages_(std::move(pages)), onReset_(std::move(onReset)) {}

HttpServer::~HttpServer() {
	Stop();
}

auto HttpServer::Start(uint16_t port, std::string* error) -> bool {
	WSADATA wsa {};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		if (error) *error = "WSAStartup failed";
		return false;
	}
	wsaStarted_ = true;

	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		if (error) *error = "socket() failed";
		return false;
	}
	// Exclusive bind: another process cannot hijack the port while we hold it.
	BOOL exclusive = TRUE;
	setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

	sockaddr_in addr {};
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 8) != 0) {
		if (error) *error = "port " + std::to_string(port) + " is already in use";
		closesocket(s);
		return false;
	}
	SetNonBlocking(s);
	listen_ = static_cast<uintptr_t>(s);
	port_   = port;
	stop_   = false;
	thread_ = std::thread([this] { Run(); });
	return true;
}

void HttpServer::Stop() {
	stop_ = true;
	if (thread_.joinable()) {
		thread_.join();
	}
	if (listen_ != ~uintptr_t { 0 }) {
		closesocket(static_cast<SOCKET>(listen_));
		listen_ = ~uintptr_t { 0 };
	}
	if (wsaStarted_) {
		WSACleanup();
		wsaStarted_ = false;
	}
}

void HttpServer::Run() {
	const SOCKET        listenSock = static_cast<SOCKET>(listen_);
	std::vector<Client> clients;

	auto handleRequest = [&](Client& c) {
		const size_t lineEnd = c.request.find("\r\n");
		const std::string line = c.request.substr(0, lineEnd);
		char method[8] {}, target[1024] {};
		if (sscanf_s(line.c_str(), "%7s %1023s", method, static_cast<unsigned>(sizeof(method)), target, static_cast<unsigned>(sizeof(target))) != 2) {
			c.pendingOut = Response(400, "Bad Request", "text/plain", "bad request");
			c.closeWhenFlushed = true;
			return;
		}
		if (!HostAllowed(HeaderValue(c.request, "host"), port_)) {
			c.pendingOut = Response(403, "Forbidden", "text/plain", "loopback only");
			c.closeWhenFlushed = true;
			return;
		}
		const std::string t    = target;
		const std::string path = t.substr(0, t.find('?'));
		constexpr const char* PageCsp =
		    "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; font-src 'none'; frame-ancestors 'none'\r\n";
		if (std::strcmp(method, "POST") == 0 && path == "/api/reset") {
			// A custom header cannot be sent cross-origin without a CORS preflight, which
			// this server never answers, so only the same-origin /control page can reset.
			if (HeaderValue(c.request, "x-loot-overlay") != "reset") {
				c.pendingOut = Response(403, "Forbidden", "text/plain", "missing reset header");
			} else {
				if (onReset_) onReset_();
				c.pendingOut = Response(200, "OK", "application/json", "{\"ok\":true}");
			}
			c.closeWhenFlushed = true;
			return;
		}
		if (std::strcmp(method, "GET") != 0) {
			c.pendingOut = Response(405, "Method Not Allowed", "text/plain", "GET only", "Allow: GET\r\n");
			c.closeWhenFlushed = true;
			return;
		}
		if (path == "/" || path == "/overlay") {
			c.pendingOut = Response(200, "OK", "text/html; charset=utf-8", pages_.overlay, PageCsp);
			c.closeWhenFlushed = true;
		} else if (path == "/tally") {
			c.pendingOut = Response(200, "OK", "text/html; charset=utf-8", pages_.tally, PageCsp);
			c.closeWhenFlushed = true;
		} else if (path == "/control") {
			c.pendingOut = Response(200, "OK", "text/html; charset=utf-8", pages_.control, PageCsp);
			c.closeWhenFlushed = true;
		} else if (path == "/tally.json") {
			c.pendingOut = Response(200, "OK", "application/json", tally_.Json());
			c.closeWhenFlushed = true;
		} else if (path == "/recent") {
			std::string body = "[";
			bool first = true;
			for (const auto& e : feed_.Recent(ReplayMax)) {
				body += first ? "" : ",";
				body += ToJson(e);
				first = false;
			}
			body += "]";
			c.pendingOut = Response(200, "OK", "application/json", body);
			c.closeWhenFlushed = true;
		} else if (path == "/health") {
			c.pendingOut = Response(200, "OK", "application/json", "{\"ok\":true,\"drops\":" + std::to_string(feed_.Count()) + "}");
			c.closeWhenFlushed = true;
		} else if (path == "/events") {
			uint32_t replay = 0;
			const std::string r = QueryParam(t, "replay");
			if (!r.empty()) {
				replay = std::min<uint32_t>(ReplayMax, static_cast<uint32_t>(std::strtoul(r.c_str(), nullptr, 10)));
			}
			c.sse        = true;
			c.pendingOut = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: keep-alive\r\nX-Content-Type-Options: nosniff\r\n\r\nretry: 2000\n\n";
			c.lastSeq    = feed_.LastSeq();
			c.lastTally  = tally_.Version();
			c.pendingOut += "event: tally\ndata: " + tally_.Json() + "\n\n";
			if (replay > 0) {
				for (const auto& e : feed_.Recent(replay)) {
					c.pendingOut += SseEvent(e);
				}
			}
			c.lastWrite = std::chrono::steady_clock::now();
		} else {
			c.pendingOut = Response(404, "Not Found", "text/plain", "not found");
			c.closeWhenFlushed = true;
		}
	};

	while (!stop_) {
		fd_set readSet, writeSet;
		FD_ZERO(&readSet);
		FD_ZERO(&writeSet);
		FD_SET(listenSock, &readSet);
		for (const auto& c : clients) {
			FD_SET(c.sock, &readSet);
			if (!c.pendingOut.empty()) {
				FD_SET(c.sock, &writeSet);
			}
		}
		timeval tv { 0, 200'000 };
		const int ready = select(0, &readSet, &writeSet, nullptr, &tv);
		const auto now  = std::chrono::steady_clock::now();

		if (ready > 0 && FD_ISSET(listenSock, &readSet)) {
			SOCKET accepted = accept(listenSock, nullptr, nullptr);
			if (accepted != INVALID_SOCKET) {
				if (clients.size() >= MaxClients) {
					closesocket(accepted);
				} else {
					SetNonBlocking(accepted);
					Client c;
					c.sock      = accepted;
					c.opened    = now;
					c.lastWrite = now;
					clients.push_back(std::move(c));
				}
			}
		}

		const uint64_t lastSeq      = feed_.LastSeq();
		const uint64_t tallyVersion = tally_.Version();
		for (auto& c : clients) {
			bool dead = false;
			if (ready > 0 && FD_ISSET(c.sock, &readSet)) {
				char buf[2048];
				const int n = recv(c.sock, buf, sizeof(buf), 0);
				if (n <= 0) {
					dead = true;
				} else if (!c.sse) {
					c.request.append(buf, static_cast<size_t>(n));
					if (c.request.size() > MaxRequestBytes) {
						dead = true;
					} else if (c.request.find("\r\n\r\n") != std::string::npos && c.pendingOut.empty()) {
						handleRequest(c);
					}
				}
			}
			if (!dead && !c.sse && c.pendingOut.empty() && now - c.opened > ReadDeadline) {
				dead = true;  // slow or idle client that never sent a full request
			}
			if (!dead && c.sse) {
				if (lastSeq > c.lastSeq) {
					for (const auto& e : feed_.Since(c.lastSeq)) {
						c.pendingOut += SseEvent(e);
						c.lastSeq = e.seq;
					}
				}
				if (tallyVersion != c.lastTally) {
					c.lastTally   = tallyVersion;
					c.pendingOut += "event: tally\ndata: " + tally_.Json() + "\n\n";
				}
				if (c.pendingOut.empty() && now - c.lastWrite > KeepAlive) {
					c.pendingOut = ": keepalive\n\n";
				}
				if (c.pendingOut.size() > 256 * 1024) {
					dead = true;  // client stopped reading
				}
			}
			if (!dead && !c.pendingOut.empty()) {
				const int n = send(c.sock, c.pendingOut.data(), static_cast<int>(std::min<size_t>(c.pendingOut.size(), 64 * 1024)), 0);
				if (n > 0) {
					c.pendingOut.erase(0, static_cast<size_t>(n));
					c.lastWrite = now;
				} else if (WSAGetLastError() != WSAEWOULDBLOCK) {
					dead = true;
				}
			}
			if (!dead && c.closeWhenFlushed && c.pendingOut.empty()) {
				shutdown(c.sock, SD_SEND);
				dead = true;
			}
			if (dead) {
				closesocket(c.sock);
				c.sock = INVALID_SOCKET;
			}
		}
		clients.erase(std::remove_if(clients.begin(), clients.end(), [](const Client& c) { return c.sock == INVALID_SOCKET; }), clients.end());
		clients_ = static_cast<uint32_t>(std::count_if(clients.begin(), clients.end(), [](const Client& c) { return c.sse; }));
	}

	for (auto& c : clients) {
		closesocket(c.sock);
	}
	clients_ = 0;
}

}
