# XashMetaPUG

Metamod-only CS 1.6/Xash3D scrim plugin inspired by MatchBot, designed for ARM64-compatible builds without AMXX, ReAPI, ReGameDLL, or x86-only helper modules.

The implementation plan is tracked at:

```txt
.sisyphus/plans/xashmetapug-metamod-scrim-plan.md
```

## Current MVP Features

- Metamod plugin entry points.
- Cvar registration.
- Chat command parsing for `.` player commands and `!` admin commands.
- Ready tracking.
- LO3 flow.
- Basic match states: warmup, waiting ready, first half, halftime, second half, overtime, finished.
- TeamScore-message based score tracking.
- Basic admin list.
- Basic pause/unpause and score commands.
- Optional "first to N wins" early match end (CS:GO MR12/MR15/MR3-OT semantics).

## Build

By default the Makefile uses the SDK headers bundled in `/Users/b4iterdev/MatchBot/MatchBot/include`:

```sh
make
```

On Linux/ARM64 this creates the deployable Metamod binary:

```txt
cstrike/addons/xashmetapug/dlls/xashmetapug_mm_arm64.so
```

On macOS, `make` is only a syntax/build smoke test and creates a Mach-O dylib under `build/`; it is not deployable to a Linux Xash3D server.

If using another SDK location:

```sh
make SDK_ROOT=/path/to/sdk/include
```

Linux output:

```txt
cstrike/addons/xashmetapug/dlls/xashmetapug_mm_arm64.so
```

## Install

Copy `cstrike/addons/xashmetapug` into the server's `cstrike/addons` directory and add this to Metamod `plugins.ini`:

```txt
linux addons/xashmetapug/dlls/xashmetapug_mm_arm64.so
```

## Player Commands

```txt
.ready
.notready
.status
.score
.help
```

## Admin Commands

```txt
!start
!forcestart
!stop
!restart
!pause
!unpause
!swap
!score
!reload
```

## Server Console Commands

```txt
xmp_forcestart
```

`xmp_forcestart` is registered as a server command through Metamod/HLSDK and is intended for server console/RCON usage. It is not a client chat command.

Admins are loaded from:

```txt
addons/xashmetapug/users.txt
```

Each non-comment line must contain an authenticated SteamID/AuthID. Player-name admin matching is intentionally not supported because names are user-controlled.

The plugin also falls back to `cstrike/addons/xashmetapug/users.txt` for local repository smoke tests, but dedicated-server installs should use the game-directory-relative `addons/xashmetapug/users.txt` path.

## Config Safety

State config cvars must stay under:

```txt
addons/xashmetapug/cfg/*.cfg
```

Paths containing `..`, spaces, quotes, semicolons, newlines, or paths outside that directory are rejected before `exec`.

## Known Limitations

- Round-end detection is inferred from `TeamScore` messages because ReGameDLL hooks are intentionally not used.
- Automatic side swapping is not yet guaranteed across Xash3D/CS builds; MVP swaps tracked scores and announces manual side switching.
- `!pause` is a minimal timed pause helper using `pausable`; exact behavior must be validated on the target Xash3D server build.
- Full damage stats are deferred because MatchBot's implementation depends on damage hooks unavailable in Metamod-only mode.
- Menu/vote systems are deferred; chat commands are the MVP interface.
- CS 1.6 (Xash3D ARM64) cannot swap the on-screen scoreboard at halftime without ReGameDLL, which is not available for ARM64. The plugin's internal scores stay in sync with the engine's team-entity counts; chat and HUD will agree throughout the match.

## Cvars

| Cvar | Default | Description |
|---|---|---|
| `xmp_enabled` | `1` | Master switch (0 disables the plugin). |
| `xmp_admin_prefix` | `!` | Chat prefix for admin commands. |
| `xmp_player_prefix` | `.` | Chat prefix for player commands. |
| `xmp_players_min` | `10` | Minimum connected players required to start a normal match (`!start`). `xmp_forcestart` bypasses this. |
| `xmp_players_max` | `10` | Display-only maximum used by `.status`. |
| `xmp_ready_type` | `1` | `1` = ready waits for player input. `2` = auto-start after `xmp_ready_time`. |
| `xmp_ready_time` | `60` | Seconds before auto-start when `xmp_ready_type=2`. |
| `xmp_match_rounds` | `30` | Total rounds per match (used when `xmp_first_to=0`). |
| `xmp_half_rounds` | `15` | Rounds that trigger halftime. |
| `xmp_first_to` | `16` | Match ends as soon as either team reaches this many wins (CS:GO MR15). Set to `0` to disable and fall back to `xmp_match_rounds`. |
| `xmp_overtime_enabled` | `1` | Allow overtime when 2nd half ends tied. |
| `xmp_overtime_rounds` | `6` | Max rounds per overtime period (used when `xmp_overtime_first_to=0`). |
| `xmp_overtime_first_to` | `4` | Overtime ends as soon as either team reaches this many wins (CS:GO MR3 OT). Set to `0` to disable and fall back to `xmp_overtime_rounds`. |
| `xmp_lo3_enabled` | `1` | Run the live-on-three restart sequence. |
| `xmp_pause_time` | `60` | Seconds the match stays paused after `!pause`. |
| `xmp_vote_percent` | `0.70` | Reserved for future vote system. |
| `xmp_cfg_warmup` | `addons/xashmetapug/cfg/warmup.cfg` | Config exec'd on entering Warmup. |
| `xmp_cfg_live` | `addons/xashmetapug/cfg/live.cfg` | Config exec'd on entering First/Second Half. |
| `xmp_cfg_halftime` | `addons/xashmetapug/cfg/halftime.cfg` | Config exec'd on entering HalfTime. |
| `xmp_cfg_overtime` | `addons/xashmetapug/cfg/overtime.cfg` | Config exec'd on entering Overtime. |
| `xmp_cfg_end` | `addons/xashmetapug/cfg/end.cfg` | Config exec'd on entering Finished. |
| `xmp_debug_messages` | `0` | Log every captured user message (verbose). |
