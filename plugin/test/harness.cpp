// Offline harness: runs the plugin's HTTP server, feed and tally without the game.
// Usage: harness.exe <port> <seconds>   (publishes a drop every second)
#include "../src/drop_feed.h"
#include "../src/http_server.h"
#include "../src/overlay_html.h"
#include "../src/tally.h"
#include "../src/tally_html.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
	const unsigned port = argc > 1 ? std::atoi(argv[1]) : 8799;
	const int secs = argc > 2 ? std::atoi(argv[2]) : 20;
	LootDrop::DropFeed feed;
	feed.SetLogPath(L"harness-drops.jsonl");
	LootDrop::Tally tally;
	tally.Load(L"harness-tally.json");
	tally.SetCharmCatalog(R"([{"id":381,"name":"Annihilus"},{"id":359,"name":"Gheed's Fortune"},{"id":1428,"name":"Valknut"},{"id":1389,"name":"Queen's Call"}])");
	LootDrop::HttpServer server(feed, tally, { LootDrop::OverlayHtml, LootDrop::TallyHtml, LootDrop::ControlHtml }, [&] { tally.Reset(); std::printf("RESET\n"); std::fflush(stdout); });
	std::string err;
	if (!server.Start(static_cast<uint16_t>(port), &err)) { std::printf("start failed: %s\n", err.c_str()); return 1; }
	std::printf("listening on %u\n", port); std::fflush(stdout);
	struct D { const char *name, *base, *quality, *code, *type, *type2; int rune; int record; };
	const D drops[] = {
		{ "Orb of Corruption", "Orb of Corruption", "normal", "ka3", "elix", "", 0, -1 },
		{ "Ber Rune", "Ber Rune", "normal", "r30", "rune", "", 30, -1 },
		{ "Perfect Ruby", "Perfect Ruby", "normal", "gpr", "gemr", "gem4", 0, -1 },
		{ "Harlequin Crest", "Shako", "unique", "uap", "helm", "", 0, -1 },
		{ "Orb of Socketing", "Orb of Socketing", "normal", "oos", "elix", "", 0, -1 },
		{ "Flawless Amethyst", "Flawless Amethyst", "normal", "gzv", "gema", "gem3", 0, -1 },
		{ "Pul Rune", "Pul Rune", "normal", "r21", "rune", "", 21, -1 },
		{ "Orb of Corruption", "Orb of Corruption", "normal", "ka3", "elix", "", 0, -1 },
		{ "Chipped Skull", "Chipped Skull", "normal", "skc", "gemz", "gem0", 0, -1 },
		{ "Annihilus", "Small Charm", "unique", "cm1", "scha", "", 0, 381 },
		{ "Gheed's Fortune", "Grand Charm", "unique", "cm3", "lcha", "", 0, 359 },
	};
	for (int i = 0; i < secs; ++i) {
		Sleep(1000);
		const D& d = drops[i % 11];
		LootDrop::DropEvent e;
		e.timeMs = LootDrop::NowEpochMs();
		e.name = d.name; e.baseName = d.base; e.quality = d.quality; e.code = d.code; e.runeNumber = d.rune; e.recordId = d.record; e.test = true;
		feed.Publish(e);
		const std::string kind = LootDrop::TallyKind(d.code, d.type, d.rune, d.quality);
		if (!kind.empty()) {
			LootDrop::TallyEntry m; m.kind = kind; m.name = d.name; m.type = d.type; m.type2 = d.type2; m.rune = d.rune; m.record = d.record;
			tally.Add(LootDrop::TallyKey(kind, d.code, d.record), m);
		}
		std::printf("published %s (tally total %llu)\n", d.name, static_cast<unsigned long long>(tally.Total())); std::fflush(stdout);
	}
	server.Stop();
	return 0;
}
