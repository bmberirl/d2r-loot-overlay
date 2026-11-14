# Loot Drop Overlay

A D2RLoader plugin for Diablo II: Resurrected (Reimagined mod) that detects
items as they drop on the ground and serves OBS browser-source overlays on
`127.0.0.1`.

- `http://127.0.0.1:8766/`: a pop-up card for each drop. Defaults to uniques,
  sets, the seven Reimagined orbs, and runes above #20.
- `http://127.0.0.1:8766/tally`: a persistent stream tally of runes (highest
  first), orbs and gems. It survives game restarts until reset.
- `http://127.0.0.1:8766/control`: a reset dock (OBS Docks > Custom Browser Docks).

URL options are documented at the top of `plugin/src/overlay_html.h` and
`plugin/src/tally_html.h`.

## How it works

The D2RLoader SDK cannot list ground items, so the plugin reads the game's
client unit table directly. It is read-only: no hooks and no writes.
It finds the table in `D2RLoader.exe`'s data by its shape (6 unit types x 128
buckets), then confirms it against the character's inventory through the SDK
before it scans. A fresh drop stays in item mode 5 ("dropping") for about 0.65 s;
the plugin scans about every 0.1 s and reports each unit id once per game.

Verified on D2R 3.4.95032 with D2RLoader 1.3.1-beta. If an update breaks
detection, scanning stops instead of crashing and the plugin logs why. Fix the
offsets in `loot-drop-overlay.ini`, then run `lootdrop-rescan`.

## Files at runtime

`%USERPROFILE%\Saved Games\Diablo II Resurrected\loot-drop-overlay\`:

- `loot-drop-overlay.ini`: port, scanner settings and native offsets
- `drops.jsonl`: every drop, one JSON object per line
- `tally.json`: the stream tally

Plugin log: `<game>\mods\Reimagined\d2rloader\logs\loot-drop-overlay.log`

## Console commands

`lootdrop-status`, `lootdrop-test [unique|set|rune|orb|gem|rare] [eth] [tally]`,
`lootdrop-probe`, `lootdrop-rescan`, `lootdrop-reset`

## Build and install

Requires the Visual Studio 2022 Build Tools (MSVC and the bundled CMake).

```bat
git submodule update --init
plugin\build.bat
```

Copy `plugin\build\loot-drop-overlay.dll` to
`<game>\mods\Reimagined\d2rloader\plugins\`, with the game closed. Install it
only for the offline Reimagined mod: ReimaginedLadder checks which plugins you
run before letting you into multiplayer games.

`plugin\test\build-harness.bat` builds an offline harness. It runs the web
server and tally without the game.

## Security

The server binds 127.0.0.1 only and rejects any Host header that is not
loopback, which blocks DNS-rebinding attacks. It is GET-only, except for
`POST /api/reset`. That route requires the `X-Loot-Overlay: reset` header,
which other websites cannot send without a CORS preflight that the server never
answers.
