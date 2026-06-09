# XashMetaPUG — Agent Guide

Metamod-only CS 1.6/Xash3D scrim plugin (ARM64). C++17, single shared library.

## Requirements

Metamod is the **only strict requirement** — the plugin works on any Metamod-enabled Xash3D/CS 1.6 server without additional modules.

ReGameDLL is **optional, detected at runtime**. If present, it provides reliable round-end events, typed spawn-equipment hooks, and team-menu interception. If absent, the plugin degrades gracefully to Metamod-only message inference (TeamScore deltas for round wins, player_weaponstrip entities for equipment, jointeam command interception for side-switch blocking).

The ReGameDLL code path is always compiled in but only activates when the running server has ReGameDLL loaded as the game DLL.

## Build

```sh
make                          # uses default SDK_ROOT=/Users/b4iterdev/MatchBot/MatchBot/include
make SDK_ROOT=/path/to/sdk    # alternate SDK
```

- **Linux/ARM64** → `cstrike/addons/xashmetapug/dlls/xashmetapug_mm_arm64.so` (deployable)
- **macOS** → `build/xashmetapug_mm.dylib` (syntax check only, NOT deployable)
- Clean: `make clean`
- CI runs on `ubuntu-24.04-arm`, clones SDK headers from `b4iterdev/MatchBot`

## SDK dependency

Headers are not in this repo. Build requires the MatchBot SDK at SDK_ROOT:
`cssdk/{common,dlls,engine,game_shared,pm_shared,public}` + `metamod/`.

Boostrap for local dev:
```sh
git clone --depth 1 https://github.com/b4iterdev/MatchBot.git /tmp/MatchBot
make SDK_ROOT=/tmp/MatchBot/MatchBot/include
```

## Source layout

```
src/
  plugin.h          — Plugin class + MatchState/PlayerInfo/ScheduledTask + Cvars structs
  plugin.cpp        — ~2380 lines, all match logic in one translation unit
  metamod_api.cpp   — Metamod entry points, engine function hooks, DLL exports
  regamedll.h/.cpp  — ReGameDLL init/shutdown, 14 hook callbacks
include/darwin-compat/  — stub Linux headers for macOS smoke-build only
cstrike/addons/xashmetapug/
  xmp.cfg           — default cvars
  users.txt         — SteamID admin list
  maps.txt          — map list
  cfg/{warmup,live,halftime,overtime,end}.cfg
```

## Architecture: Dual code paths

The plugin has **two parallel systems** that must be kept in sync:

1. **Metamod message interception** (always active): hooks `pfnMessageBegin/Write*/MessageEnd` to intercept `TeamScore`, `ScoreInfo`, `TeamInfo`, `Money`, `VGUIMenu`, `ShowMenu`, `TextMsg`. Used for scoreboard overlay and as fallback.

2. **ReGameDLL hook chains** (optional, detected at runtime): registers 14 typed C++ hooks (`RoundEnd`, `OnSpawnEquip`, `HandleMenu_ChooseTeam`, etc.). If ReGameDLL is absent, plugin degrades to Metamod-only inference.

Never remove the Metamod message path — ReGameDLL may not be present on all servers.

## Key state machine

`MatchState` enum: `Disabled → Warmup → WaitingReady → KnifeRound → SideSelection → StartingLO3 → FirstHalf → HalfTime → SecondHalf → Overtime → Finished`

Transitions handled in `SetState()` which calls `ExecuteStateConfig()` + `ApplyStateRules()`.

## Score tracking quirk

Round-end is detected ONE of two ways:
- **ReGameDLL path**: `OnRoundEnd()` hook — authoritative
- **Metamod fallback**: intercept `TeamScore` user messages and infer deltas (fragile)

Scoreboard overlay: plugin maintains its own `displayedTerroristScore_`/`displayedCTScore_` and resends `TeamScore` messages to clients. Must sync after halftime, LO3, and pause.

## Pause implementation (not `pausable`)

Does NOT use `pausable` entity. Instead:
1. Sets `mp_freezetime`/`mp_buytime` to match pause duration
2. Forces `m_bFreezePeriod = TRUE` every frame in `OnStartFrame`
3. Resets `m_fRoundStartTime` to keep round frozen
4. Unpause restores original cvars, runs `sv_restart 1`

## Weapon control (no CCSPlayer API)

Currently uses native Metamod entities (`player_weaponstrip`) and direct `CBasePlayer` member access:
- `StripPlayerWeaponsNative()` — spawns `player_weaponstrip`, calls `MDLL_Use`
- `GiveItemNative()` — spawns item entity, calls `MDLL_Touch`
- `SetPlayerMoneyNative()` — writes `m_iAccount` + sends `Money` message
- C4 removed via `ForEachItem` + `RemovePlayerItem`

## LO3 (Live On 3)

Scheduled via task system, not real-time:
```cpp
// In StartLO3():
lo3_step = 0;
Schedule("lo3", 1.0f, true, callback that increments lo3_step);
// step 1: sv_restart 1, step 2: sv_restart 3, then 3s later FinishLO3()
```

## Task scheduler

Built-in `Schedule(name, delay, repeat, callback)` runs callbacks from `OnStartFrame()`. No OS timers. Tasks cancelable by name. Used for knife-strip timing, LO3 steps, pause countdowns, practice enforcement, class selection retries.

## Test suite

```sh
python3 tests/test_knife_round_static.py
```

Static analysis only — reads source files as text, asserts string patterns exist. No runtime/server needed. Located in `tests/test_knife_round_static.py`.

## Cvar conventions

All cvars prefixed `xmp_`. Registration in `RegisterCvars()` via `g_engfuncs.pfnCVarRegister`. Helpers `CvarInt()`, `CvarFloat()`, `CvarString()` wrap the raw `cvar_t.value/string`.

## Config safety

State config paths validated by `IsSafeConfigPath()`: must start with `addons/xashmetapug/cfg/`, no `..`, no special chars, must end in `.cfg`, max 120 chars. Rejected paths are logged and not `exec`'d.

## Admin system

Admins loaded from `addons/xashmetapug/users.txt` (or `cstrike/addons/xashmetapug/users.txt` fallback). Each line = SteamID/AuthID. Player-name matching intentionally unsupported. Admins matched via `players_[index].admin` flag set in `UpdatePlayer()` by checking `authId` against `admins_` set.

## Testing live behavior

Server commands at console:
- `xmp_forcestart`, `xmp_start`, `xmp_stop`, `xmp_restart`, `xmp_pause`, `xmp_unpause`, `xmp_swap`, `xmp_score`, `xmp_reload`, `xmp_timeout`, `xmp_tech`
- Tweak cvars: `xmp_players_min 2`, `xmp_half_rounds 1`, `xmp_match_rounds 2` for fast matches
- Debug: `xmp_debug_messages 1` logs every intercepted user message

## Deploy

1. Copy `cstrike/addons/xashmetapug/` to server's `cstrike/addons/`
2. Add to Metamod `plugins.ini`:
   ```
   linux addons/xashmetapug/dlls/xashmetapug_mm_arm64.so
   ```

## Known risks for agents

- Always build-test on Linux/ARM64 or use the smoke macOS build. The darwin-compat stubs are fragile.
- Round-end inference from TeamScore is unreliable — if adding new match flow logic, prefer the ReGameDLL path.
- The `Schedule()` system is frame-based, not real-time. `gpGlobals->time` resolution depends on server FPS.
- Admin authId is populated asynchronously — `UpdatePlayer()` may run before auth is resolved. Ready/admin checks may be racy on connect.
- `SyncDisplayedTeamScoresFromMatchScores()` must be called after any score change that affects the client scoreboard.
- Adding new message interception requires adding a matching `OnWrite*` method and the `MessageCapture` tracking.
- The `!` and `.` command prefixes are configurable via cvars — never hardcode them.
