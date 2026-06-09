# XashMetaPUG

Metamod-only CS 1.6/Xash3D scrim plugin inspired by MatchBot, designed for ARM64-compatible builds without AMXX, ReAPI, or x86-only helper modules.

**Requires:** Metamod. **Optional:** ReGameDLL — if present on the server, the plugin uses it for reliable round-end events and equipment hooks. If absent, the plugin degrades gracefully to Metamod-only message inference.

## Current MVP Features

- Metamod plugin entry points, optional ReGameDLL hooks.
- Cvar registration.
- Chat command parsing for `.` player commands and `!` admin commands.
- Ready tracking.
- Knife round before first LIVE with player-selected `.stay` / `.swap` side choice.
- LO3 flow with center-text announcements.
- Match states: warmup, waiting ready, knife round, side selection, first half, halftime, second half, overtime, finished.
- TeamScore-message based score tracking and scoreboard display management.
- Config execution on state transitions (warmup, live, halftime, overtime, end).
- Team name setting via `.teamname` command.
- Player timeouts: `.timeout` (30s) and `.tech` (indefinite, both teams `.unpause`).
- Admin and server console commands for match management.
- Overtime with configurable round count, auto side-swap mid-OT, and score preservation.
- Optional "first to N wins" early match end (CS:GO MR12/MR15/MR3-OT semantics).
- Admin list loaded from `users.txt`.
- Periodic command reminders during Warmup and HalfTime.

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

| Command | Description |
|---|---|
| `.ready` | Mark yourself ready. |
| `.notready` | Mark yourself not ready. |
| `.stay` | Keep your current side after winning knife round. |
| `.swap` | Swap sides after winning knife round. |
| `.teamname <name>` | Set your team's name (max 32 chars). |
| `.status` | Show match state, ready count, player count, team names. |
| `.score` | Show current score. |
| `.timeout` | Call a 30-second tactical timeout (during live match). |
| `.tech` | Call a technical timeout (both teams must `.unpause` to resume). |
| `.unpause` | Vote to unpause during a technical timeout (both teams must agree). |
| `.help` | Show available commands. |

## Admin Commands

| Command | Description |
|---|---|
| `!start` | Start the ready phase. |
| `!forcestart` | Start the match immediately (bypasses player count and team name checks). |
| `!stop` | Stop the match. |
| `!restart` | Restart the match from warmup. |
| `!pause` | Pause the match. |
| `!unpause` | Unpause the match. |
| `!swap` | Swap teams (disabled while live). |
| `!score` | Display current score. |
| `!reload` | Reload the admin list from `users.txt`. |

## Server Console Commands

All admin commands are also available as server console commands with an `xmp_` prefix:

| Command | Description |
|---|---|
| `xmp_forcestart` | Start the match immediately (bypasses player count/team name checks). |
| `xmp_start` | Start the ready phase. |
| `xmp_stop` | Stop the match. |
| `xmp_restart` | Restart the match from warmup. |
| `xmp_pause` | Pause the match. |
| `xmp_unpause` | Unpause the match. |
| `xmp_swap` | Swap teams (disabled while live). |
| `xmp_score` | Display current score. |
| `xmp_reload` | Reload the admin list from `users.txt`. |
| `xmp_timeout` | Trigger a 30-second tactical timeout. |
| `xmp_tech` | Trigger a technical timeout. |

## Admin List

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
- Knife-round winner detection is also inferred from `TeamScore`; the plugin gates side choice before first LIVE and uses a delayed native Metamod strip pass (`player_weaponstrip`, `weapon_knife`) plus CS private player data / `Money` user messages to enforce knife-only, no-money rounds without repeatedly re-giving the knife. Buy menus remain accessible, but players have no money during the knife round.
- Automatic side swapping is handled via `SwapTeams()` during halftime and overtime; name tracking follows the team on swap.
- `!pause` is a minimal timed pause helper using `pausable` — replaced with a round-transition timeout implementation that sets `mp_freezetime`/`mp_buytime`. Exact behavior must be validated on the target Xash3D server build.
- Full damage stats are deferred because MatchBot's implementation depends on damage hooks unavailable in Metamod-only mode.
- Menu/vote systems are deferred; chat commands are the MVP interface.
- Team scores shown on the client scoreboard are managed through intercepted/resend `TeamScore` user messages. The plugin keeps its own displayed T/CT score layer and resends those values after halftime, LO3, and score updates.

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
| `xmp_timeout_time` | `30` | Seconds for a tactical timeout (`.timeout`). |
| `xmp_vote_percent` | `0.70` | Reserved for future vote system. |
| `xmp_cfg_warmup` | `addons/xashmetapug/cfg/warmup.cfg` | Config exec'd on entering Warmup. |
| `xmp_cfg_live` | `addons/xashmetapug/cfg/live.cfg` | Config exec'd on entering First/Second Half. |
| `xmp_cfg_halftime` | `addons/xashmetapug/cfg/halftime.cfg` | Config exec'd on entering HalfTime. |
| `xmp_cfg_overtime` | `addons/xashmetapug/cfg/overtime.cfg` | Config exec'd on entering Overtime. |
| `xmp_cfg_end` | `addons/xashmetapug/cfg/end.cfg` | Config exec'd on entering Finished. |
| `xmp_debug_messages` | `0` | Log every captured user message (verbose). |
