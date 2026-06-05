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
