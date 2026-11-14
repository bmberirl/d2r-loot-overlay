// Loot Drop Overlay: announces items as they drop on the ground and serves an
// OBS browser-source overlay on 127.0.0.1. Client role, read-only, no hooks.

#include "drop_feed.h"
#include "ground_scanner.h"
#include "http_server.h"
#include "item_tables.h"
#include "overlay_html.h"
#include "settings.h"
#include "tally.h"
#include "tally_html.h"

#include <D2RLPlugin/api.h>

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

using namespace LootDrop;

constexpr const char* PluginId = "loot-drop-overlay";

static constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Loot Drop Overlay",
	.version     = "0.1.0",
	.author      = "bmberirl",
	.description = "Announces ground drops and serves an OBS browser-source overlay on 127.0.0.1.",
	.flags       = D2RL::PluginFlags::Client,
};

struct State {
	const D2RL::PluginContext*       ctx       = nullptr;
	const D2RL::ThreadService*       threads   = nullptr;
	const D2RL::LifecycleService*    lifecycle = nullptr;
	const D2RL::InventoryService*    inventory = nullptr;
	Settings                         settings;
	std::filesystem::path            dataDir;
	DropFeed                         feed;
	std::unique_ptr<HttpServer>      server;
	ItemTables                       tables;
	Tally                            tally;
	GroundScanner                    scanner;
	std::atomic<bool>                unloading { false };
	std::thread                      watchdog;
	bool                             inGame    = false;
	bool                             needSetup = false;
	uint32_t                         setupTries = 0;
	int32_t                          levelId   = -1;
	std::atomic<uint64_t>            frame { 0 };
	std::atomic<bool>                tickQueued { false };
	std::string                      scannerStatus = "waiting for a game";
};

State* g = nullptr;

void Log(const std::string& text) {
	if (g && g->ctx) g->ctx->LogInfo(("[loot-drop] " + text).c_str());
}
void Say(const std::string& text) {
	if (g && g->ctx) g->ctx->WriteConsoleMessage(("loot-drop: " + text).c_str());
}

auto NowMs() -> int64_t {
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<int64_t>((t - 116444736000000000ULL) / 10000ULL);
}

auto QualityName(uint32_t q) -> const char* {
	static const char* names[] = { "unknown", "inferior", "normal", "superior", "magic", "set", "rare", "unique", "crafted" };
	return q < 9 ? names[q] : "unknown";
}

constexpr uint32_t FlagIdentified = 0x00000010;
constexpr uint32_t FlagSocketed   = 0x00000800;
constexpr uint32_t FlagEthereal   = 0x00400000;
constexpr uint32_t FlagRuneword   = 0x04000000;

// Publishes a drop and, for orbs, runes, gems and unique charms, adds it to the stream tally.
void Record(const DropEvent& e) {
	g->feed.Publish(e);
	std::string type, type2;
	if (const BaseItem* base = e.test ? g->tables.BaseByCode(e.code) : g->tables.Base(e.classId)) {
		type  = base->type;
		type2 = base->type2;
	}
	const std::string kind = TallyKind(e.code, type, e.runeNumber, e.quality);
	if (kind.empty()) return;
	TallyEntry meta;
	meta.kind   = kind;
	meta.name   = e.name;
	meta.type   = type;
	meta.type2  = type2;
	meta.rune   = e.runeNumber;
	meta.record = e.recordId;
	g->tally.Add(TallyKey(kind, e.code, e.recordId), meta);
}

auto CharmCatalogJson() -> std::string {
	std::string json = "[";
	bool first = true;
	for (const auto& [id, name] : g->tables.UniqueCharms()) {
		json += first ? "" : ",";
		json += "{\"id\":" + std::to_string(id) + ",\"name\":\"" + JsonEscape(name) + "\"}";
		first = false;
	}
	return json + "]";
}

auto MakeEvent(const GroundItem& item) -> DropEvent {
	DropEvent e;
	e.timeMs     = NowMs();
	e.unitId     = item.unitId;
	e.classId    = item.classId;
	e.flags      = item.flags;
	e.ethereal   = (item.flags & FlagEthereal) != 0;
	e.identified = (item.flags & FlagIdentified) != 0;
	e.socketed   = (item.flags & FlagSocketed) != 0;
	e.runeword   = (item.flags & FlagRuneword) != 0;
	e.quality    = QualityName(item.quality);
	e.levelId    = g->levelId;
	e.x          = item.x;
	e.y          = item.y;
	if (const BaseItem* base = g->tables.Base(item.classId)) {
		e.code     = base->code;
		e.baseName = base->name;
	} else {
		e.code     = "#" + std::to_string(item.classId);
		e.baseName = e.code;
	}
	e.runeNumber = RuneNumber(e.code);
	if (item.quality == 7) e.name = g->tables.UniqueName(item.fileIndex);
	if (item.quality == 5) e.name = g->tables.SetName(item.fileIndex);
	if (item.quality == 5 || item.quality == 7) e.recordId = static_cast<int32_t>(item.fileIndex);
	if (e.name.empty()) e.name = e.baseName;
	return e;
}

// ---- inventory sample (SDK) used to calibrate names and verify the native layout ----

struct Sample {
	std::vector<KnownItem> known;
	uint32_t               calibrations = 0;
};

auto CollectInventory(Sample* sample) -> bool {
	if (!g->inventory || !g->inventory->getLocalPlayer || !g->inventory->forEachInventoryItem) {
		return false;
	}
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g->inventory->getLocalPlayer(g->ctx, &player) != D2RL::Inventory::Result::Success) {
		return false;
	}
	D2RL::Inventory::ItemFilter filter { sizeof(filter), 0, D2RL::Items::AllItemContainers, 0 };
	auto cb = [](const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* user) noexcept -> D2RL::Inventory::IterationAction {
		auto* s = static_cast<Sample*>(user);
		if (info && info->runtimeId != 0) {
			s->known.push_back({ info->runtimeId, info->classId, static_cast<uint32_t>(info->quality) });
			char code[5] = { static_cast<char>(info->code & 0xFF), static_cast<char>((info->code >> 8) & 0xFF), static_cast<char>((info->code >> 16) & 0xFF),
			                 static_cast<char>((info->code >> 24) & 0xFF), 0 };
			g->tables.Calibrate(info->classId, code);
			++s->calibrations;
		}
		return s->known.size() >= 64 ? D2RL::Inventory::IterationAction::Stop : D2RL::Inventory::IterationAction::Continue;
	};
	return g->inventory->forEachInventoryItem(g->ctx, player, &filter, cb, sample) == D2RL::Inventory::Result::Success;
}

void SetupScanner() {
	Sample sample;
	if (!CollectInventory(&sample)) {
		g->scannerStatus = "waiting: inventory service not ready";
		return;
	}
	std::string why;
	bool ok = false;
	if (g->scanner.UsesAutoLocate()) {
		ok = g->scanner.AutoLocate(sample.known, &why);
	} else if (g->scanner.Resolved()) {
		ok = g->scanner.Validate(sample.known, &why);
		if (!ok && !g->settings.requireLayoutCheck) {
			g->scanner.ForceValidated();
			ok  = true;
			why += " (check disabled in settings)";
		}
	} else {
		why = "unit table unresolved";
	}
	g->needSetup     = !ok && ++g->setupTries < 5;  // retry a few times (items may not be loaded yet)
	g->scannerStatus = (ok ? "scanning; " : "not scanning; ") + why;
	g->scanner.ResetSeen();
	if (ok) {
		g->scanner.Scan(false);  // mark everything already on the floor as seen
	}
	Log(g->scannerStatus);
	Log(g->tables.CalibrationSummary());
}

// ---- UI-thread tick: runs every root-widget update and re-queues itself ----

using IsInGameFn = bool(__cdecl*)() noexcept;

void EnterGame(const char* source) {
	g->inGame     = true;
	g->setupTries = 0;
	g->needSetup  = g->settings.scannerEnabled;
	g->scanner.ResetSeen();
	g->scannerStatus = "in game; locating the item table";
	Log(std::string("in game (") + source + ")");
}

void LeaveGame(const char* source) {
	g->inGame    = false;
	g->needSetup = false;
	g->scanner.ResetSeen();
	g->scannerStatus = "waiting for a game";
	Log(std::string("left game (") + source + ")");
}

void __cdecl Tick(const D2RL::PluginContext*, void*) noexcept {
	if (!g || g->unloading) return;
	g->tickQueued = false;
	if (++g->frame == 1) Log("UI tick running");
	// Fallback for when gameplay events do not arrive: poll D2RCore's IsInGame twice a second.
	static IsInGameFn isInGame = reinterpret_cast<IsInGameFn>(GetProcAddress(GetModuleHandleW(L"D2RCore.dll"), "IsInGame"));
	if (isInGame && g->frame % 30 == 0) {
		const bool now = isInGame();
		if (now && !g->inGame) EnterGame("IsInGame poll");
		if (!now && g->inGame) LeaveGame("IsInGame poll");
	}
	if (g->inGame) {
		if (g->needSetup && g->frame % 30 == 0) {
			g->needSetup = false;
			SetupScanner();
		}
		if (g->settings.scannerEnabled && g->scanner.Ready() && g->frame % g->settings.scanEveryFrames == 0) {
			for (const auto& item : g->scanner.Scan(g->settings.droppingOnly)) {
				Record(MakeEvent(item));
			}
		}
	}
	if (!g->unloading && g->threads) {
		const auto r = g->threads->runOnUiThread(g->ctx, Tick, nullptr);
		g->tickQueued = r == D2RL::Threads::Result::Success;
		static bool warned = false;
		if (r != D2RL::Threads::Result::Success && !warned) {
			warned = true;
			Log("UI tick could not re-queue, result " + std::to_string(static_cast<uint32_t>(r)));
		}
	}
}

void __cdecl OnGameplay(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* ev, void*) noexcept {
	if (!g || !ev) return;
	Log("gameplay event " + std::to_string(static_cast<uint32_t>(ev->kind)) + " value " + std::to_string(ev->currentValue));
	switch (ev->kind) {
	case D2RL::Lifecycle::GameplayEventKind::GameJoined:
		if (!g->inGame) EnterGame("GameJoined");
		break;
	case D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady:
		g->setupTries = 0;
		g->needSetup  = g->settings.scannerEnabled;
		g->inGame     = true;
		break;
	case D2RL::Lifecycle::GameplayEventKind::LevelChanged:
		g->levelId = ev->currentValue;
		break;
	case D2RL::Lifecycle::GameplayEventKind::GameLeft:
		if (g->inGame) LeaveGame("GameLeft");
		break;
	default: break;
	}
}

// ---- console commands ----

auto __cdecl CmdStatus(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept -> D2RL::ConsoleCommandResult {
	char buf[512];
	std::snprintf(buf, sizeof(buf), "overlay http://127.0.0.1:%u/  (%u OBS client(s)), %llu drop(s) this session", g->server ? g->server->Port() : 0, g->server ? g->server->ClientCount() : 0,
	              static_cast<unsigned long long>(g->feed.Count()));
	Say(buf);
	Say("scanner: " + g->scannerStatus);
	std::snprintf(buf, sizeof(buf), "item units last scan: %u, read faults: %llu, level %d", g->scanner.LastCount(), static_cast<unsigned long long>(g->scanner.Faults()), g->levelId);
	Say(buf);
	Say(g->tables.CalibrationSummary());
	Say("log: " + std::filesystem::path(g->dataDir / L"drops.jsonl").string());
	Say("tally: " + std::to_string(g->tally.Total()) + " orbs/runes/gems; overlay /tally, reset with lootdrop-reset or /control");
	return D2RL::ConsoleCommandResult::Handled;
}

auto __cdecl CmdReset(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept -> D2RL::ConsoleCommandResult {
	g->tally.Reset();
	Log("tally reset from the console");
	Say("tally reset.");
	return D2RL::ConsoleCommandResult::Handled;
}

auto __cdecl CmdTest(D2R::Game::Client*, const D2RL::ConsoleCommandContext* cmd, void*) noexcept -> D2RL::ConsoleCommandResult {
	const std::string arg = cmd && cmd->args ? std::string(cmd->args, cmd->argsLength) : std::string {};
	DropEvent e;
	e.timeMs  = NowMs();
	e.test    = true;
	e.levelId = g->levelId;
	if (arg.find("set") != std::string::npos) {
		e.quality = "set"; e.name = "Tal Rasha's Guardianship"; e.baseName = "Lacquered Plate"; e.code = "uth";
	} else if (arg.find("rune") != std::string::npos) {
		e.quality = "normal"; e.name = "Ber Rune"; e.baseName = "Ber Rune"; e.code = "r30"; e.runeNumber = 30;
	} else if (arg.find("orb") != std::string::npos) {
		e.quality = "normal"; e.name = "Orb of Corruption"; e.baseName = "Orb of Corruption"; e.code = "ka3";
	} else if (arg.find("charm") != std::string::npos) {
		e.quality = "unique"; e.name = "Annihilus"; e.baseName = "Small Charm"; e.code = "cm1"; e.recordId = 381;
	} else if (arg.find("gem") != std::string::npos) {
		e.quality = "normal"; e.name = "Perfect Ruby"; e.baseName = "Perfect Ruby"; e.code = "gpr";
	} else if (arg.find("rare") != std::string::npos) {
		e.quality = "rare"; e.name = "Diadem"; e.baseName = "Diadem"; e.code = "ci3";
	} else {
		e.quality = "unique"; e.name = "Harlequin Crest"; e.baseName = "Shako"; e.code = "uap"; e.ethereal = arg.find("eth") != std::string::npos;
	}
	// Test drops only reach the stream tally when asked ("lootdrop-test orb tally").
	if (arg.find("tally") != std::string::npos) {
		Record(e);
		Say("test drop sent and tallied: " + e.name);
	} else {
		g->feed.Publish(e);
		Say("test drop sent: " + e.name + " (add 'tally' to count it)");
	}
	return D2RL::ConsoleCommandResult::Handled;
}

auto __cdecl CmdProbe(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!g->scanner.Resolved()) {
		Say("unit table not resolved: " + g->scannerStatus);
		return D2RL::ConsoleCommandResult::Failed;
	}
	const auto all = g->scanner.All();
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%zu item units:", all.size());
	Say(buf);
	size_t shown = 0;
	for (const auto& u : all) {
		if (u.mode != 3 && u.mode != 5 && shown >= 4) continue;  // favour ground items
		const BaseItem* base = g->tables.Base(u.classId);
		std::snprintf(buf, sizeof(buf), "  id=%u class=%u (%s) mode=%u quality=%u file=%u flags=%08X at %d,%d", u.unitId, u.classId, base ? base->code.c_str() : "?", u.mode, u.quality,
		              u.fileIndex, u.flags, u.x, u.y);
		Say(buf);
		Log(buf);
		if (++shown >= 16) break;
	}
	return D2RL::ConsoleCommandResult::Handled;
}

auto __cdecl CmdRescan(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept -> D2RL::ConsoleCommandResult {
	// Re-read the ini so offset fixes apply without restarting the game.
	g->settings = LoadSettings(g->dataDir / L"loot-drop-overlay.ini");
	std::string why;
	g->scanner.Init(g->ctx->exeBase, g->settings.layout, &why);
	g->setupTries = 0;
	SetupScanner();
	Say("settings reloaded; scanner: " + g->scannerStatus);
	return D2RL::ConsoleCommandResult::Handled;
}

auto SavedGamesDir() -> std::filesystem::path {
	PWSTR path = nullptr;
	std::filesystem::path out;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &path)) && path) {
		out = std::filesystem::path(path) / L"Diablo II Resurrected" / L"loot-drop-overlay";
	}
	CoTaskMemFree(path);
	return out;
}

template <typename Service>
auto Query(const D2RL::PluginContext* ctx) -> const Service* {
	const Service* s = nullptr;
	return ctx->QueryService(&s) == D2RL::ServiceQueryResult::Success ? s : nullptr;
}

}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) return false;
	try {
		g      = new State();
		g->ctx = context;

		g->dataDir = SavedGamesDir();
		std::error_code ec;
		std::filesystem::create_directories(g->dataDir, ec);
		const auto ini = g->dataDir / L"loot-drop-overlay.ini";
		EnsureDefaultSettings(ini);
		g->settings = LoadSettings(ini);
		g->feed.SetLogPath(g->dataDir / L"drops.jsonl");

		g->threads   = Query<D2RL::ThreadService>(context);
		g->lifecycle = Query<D2RL::LifecycleService>(context);
		g->inventory = Query<D2RL::InventoryService>(context);
		if (!g->threads || !g->lifecycle) {
			context->LogError("[loot-drop] thread or lifecycle service unavailable; plugin disabled.");
			delete g;
			g = nullptr;
			return false;
		}

		if (context->modDirectory) {
			Log(g->tables.Load(context->modDirectory));
		}

		std::string why;
		g->scanner.Init(context->exeBase, g->settings.layout, &why);
		if (!why.empty()) Log(why);

		g->tally.Load(g->dataDir / L"tally.json");
		g->tally.SetCharmCatalog(CharmCatalogJson());
		Log("unique charms known: " + std::to_string(g->tables.UniqueCharms().size()));
		Log("tally loaded: " + std::to_string(g->tally.Total()) + " orbs/runes/gems so far");
		g->server = std::make_unique<HttpServer>(g->feed, g->tally, HttpServer::Pages { OverlayHtml, TallyHtml, ControlHtml }, [] {
			if (g) {
				g->tally.Reset();
				Log("tally reset from the control page");
			}
		});
		std::string err;
		if (g->server->Start(g->settings.port, &err)) {
			Log("overlay at http://127.0.0.1:" + std::to_string(g->settings.port) + "/");
		} else {
			context->LogWarn(("[loot-drop] overlay server not started: " + err + " (change port in loot-drop-overlay.ini)").c_str());
		}

		for (const auto kind : { D2RL::Lifecycle::GameplayEventKind::GameJoined, D2RL::Lifecycle::GameplayEventKind::GameLeft, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, D2RL::Lifecycle::GameplayEventKind::LevelChanged }) {
			D2RL::Lifecycle::GameplayEventListener listener { sizeof(listener), 0, kind, 0, OnGameplay, nullptr };
			D2RL::Lifecycle::ListenerHandle handle {};
			const auto r = g->lifecycle->registerGameplayEventListener(context, &listener, &handle);
			if (r != D2RL::Lifecycle::Result::Success) {
				context->LogWarn(("[loot-drop] gameplay listener " + std::to_string(static_cast<uint32_t>(kind)) + " not registered, result " + std::to_string(static_cast<uint32_t>(r))).c_str());
			}
		}

		bool commands = context->RegisterConsoleCommand("lootdrop-status", CmdStatus, "Loot Drop Overlay: show overlay URL and scanner state.");
		commands &= context->RegisterConsoleCommand("lootdrop-test", CmdTest, "Loot Drop Overlay: send a test drop (unique|set|rune|orb|gem|charm|rare, add eth, add tally to count it).");
		commands &= context->RegisterConsoleCommand("lootdrop-probe", CmdProbe, "Loot Drop Overlay: list item units the scanner sees.");
		commands &= context->RegisterConsoleCommand("lootdrop-rescan", CmdRescan, "Loot Drop Overlay: locate and verify the unit table again.");
		commands &= context->RegisterConsoleCommand("lootdrop-reset", CmdReset, "Loot Drop Overlay: clear the stream tally (orbs, runes, gems).");
		Log(commands ? "console commands registered" : "some console commands failed to register");

		// The loader marks the plugin active only after this function returns, so a queue
		// attempt here gets OwnerInactive. A watchdog thread queues the tick until it runs,
		// and again whenever it stops advancing.
		g->watchdog = std::thread([] {
			uint64_t lastFrame = ~0ULL;
			uint32_t attempts  = 0;
			uint32_t stalled   = 0;
			while (g && !g->unloading) {
				for (int i = 0; i < 10 && !g->unloading; ++i) Sleep(100);
				if (g->unloading) break;
				const uint64_t frame = g->frame;
				stalled = (frame == lastFrame) ? stalled + 1 : 0;
				// Queue when nothing is pending, or when a pending tick has not run for 10 s
				// (queued work can be discarded, e.g. on a session change).
				if ((frame == lastFrame || frame == 0) && (!g->tickQueued || stalled >= 10)) {
					stalled = 0;
					const auto r = g->threads->runOnUiThread(g->ctx, Tick, nullptr);
					if (r == D2RL::Threads::Result::Success) g->tickQueued = true;
					if (++attempts <= 3 || r == D2RL::Threads::Result::Success) {
						Log("UI tick queue attempt " + std::to_string(attempts) + ", result " + std::to_string(static_cast<uint32_t>(r)));
					}
				}
				lastFrame = frame;
			}
		});
		return true;
	} catch (...) {
		context->LogError("[loot-drop] load failed.");
		delete g;
		g = nullptr;
		return false;
	}
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	if (!g) return;
	g->unloading = true;
	if (g->watchdog.joinable()) g->watchdog.join();
	if (g->server) g->server->Stop();
	delete g;
	g = nullptr;
}
