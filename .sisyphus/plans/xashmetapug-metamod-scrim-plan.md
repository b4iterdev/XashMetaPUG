# XashMetaPUG Metamod-only Scrim Plugin Plan

## Goal

Create an ARM64-compatible, Metamod-only CS 1.6 plugin for Xash3D that provides MatchBot-like scrim setup and match control without AMXX, ReAPI, ReGameDLL, external services, or x86-only binary modules.

## Reference Behavior

Reference repository: `/Users/b4iterdev/MatchBot`.

MatchBot capabilities to emulate in staged form:

- Warmup and ready system.
- Match lifecycle: disabled/dead, warmup, starting, first half, halftime, second half, overtime, finished.
- Player commands: `.ready`, `.notready`, `.status`, `.score`, `.help`.
- Admin commands: `!start`, `!stop`, `!restart`, `!pause`, `!unpause`, `!swap`, `!score`.
- Live-on-three restart flow.
- MR15 / configurable max-round match handling.
- Halftime and score preservation.
- Overtime if tied.
- Config-per-state execution.
- Basic admin flag system.
- Chat/HUD/console messaging.

Deferred from MVP:

- Full damage stats.
- Captain/team pickup modes.
- Skill balancing.
- Knife round.
- Complex vote menus.
- Warmup deathmatch.
- Weapon restrictions.
- Anti-ragequit bans.
- Web/Discord/database integrations.

## Constraint Notes

MatchBot relies on ReGameDLL/ReAPI hooks such as `RoundEnd`, `InternalCommand`, `CBasePlayer_TakeDamage`, and `CSGameRules_RestartRound`. XashMetaPUG must not depend on those. It must infer events through Metamod/Half-Life SDK hooks and engine messages.

## Target Project Layout

```txt
XashMetaPUG/
  Makefile
  README.md
  cstrike/
    addons/
      metamod/plugins.ini
      xashmetapug/
        xmp.cfg
        users.txt
        maps.txt
        cfg/
          warmup.cfg
          live.cfg
          halftime.cfg
          overtime.cfg
          end.cfg
        dlls/
  src/
    admin.cpp/.h
    commands.cpp/.h
    config.cpp/.h
    messages.cpp/.h
    metamod_api.cpp/.h
    plugin.cpp/.h
    scheduler.cpp/.h
    state.cpp/.h
    util.cpp/.h
```

## MVP State Machine

```cpp
enum class MatchState {
    Disabled,
    Warmup,
    WaitingReady,
    StartingLO3,
    FirstHalf,
    HalfTime,
    SecondHalf,
    Overtime,
    Finished
};
```

Expected transitions:

```txt
Disabled -> Warmup -> WaitingReady -> StartingLO3 -> FirstHalf
FirstHalf -> HalfTime -> WaitingReady -> StartingLO3 -> SecondHalf
SecondHalf -> Finished or Overtime
Overtime -> Finished or next Overtime block
```

## Required Cvars

- `xmp_enabled 1`
- `xmp_admin_prefix !`
- `xmp_player_prefix .`
- `xmp_players_min 10`
- `xmp_players_max 10`
- `xmp_ready_type 1`
- `xmp_ready_time 60`
- `xmp_match_rounds 30`
- `xmp_half_rounds 15`
- `xmp_overtime_enabled 1`
- `xmp_overtime_rounds 6`
- `xmp_lo3_enabled 1`
- `xmp_pause_time 60`
- `xmp_vote_percent 0.70`
- `xmp_cfg_warmup addons/xashmetapug/cfg/warmup.cfg`
- `xmp_cfg_live addons/xashmetapug/cfg/live.cfg`
- `xmp_cfg_halftime addons/xashmetapug/cfg/halftime.cfg`
- `xmp_cfg_overtime addons/xashmetapug/cfg/overtime.cfg`
- `xmp_cfg_end addons/xashmetapug/cfg/end.cfg`

## Implementation Milestones

### Milestone 1: Plugin skeleton

- Implement Metamod exports.
- Register cvars.
- Add portable Makefile for ARM64 Linux shared object.
- Add deployment layout and `plugins.ini`.

QA scenario:

- Tool: shell build command and Xash3D dedicated server console.
- Steps:
  1. Run `make` from `/Users/b4iterdev/XashMetaPUG`.
  2. Copy/build output to `cstrike/addons/xashmetapug/dlls/` if not already staged.
  3. Start Xash3D CS server with Metamod enabled.
  4. Run `meta list` in server console.
  5. Query `xmp_enabled` in server console.
- Expected results:
  - Build exits 0 when SDK paths are configured.
  - `meta list` shows XashMetaPUG loaded/running.
  - `xmp_enabled` exists and defaults to `1`.
  - Plugin logs startup message without crashes.

### Milestone 2: Commands and admin

- Intercept `ClientCommand` via DLL hook table.
- Parse `say` and `say_team` command text.
- Dispatch player/admin prefixes.
- Load admin flags from `cstrike/addons/xashmetapug/users.txt`.

QA scenario:

- Tool: CS 1.6/Xash3D client connected to the test server plus server console logs.
- Steps:
  1. Connect as a normal user and type `say .status`.
  2. Type `say .ready` and `say .notready`.
  3. Type `say !start` as a non-admin.
  4. Add the user's SteamID/name to `users.txt` with admin flags, reload/change map, and type `say !start` again.
- Expected results:
  - `.status`, `.ready`, `.notready` are intercepted and do not appear as normal chat.
  - Non-admin `!start` is denied.
  - Admin `!start` is accepted and advances match control flow.
  - No unknown-command spam or server crash occurs.

### Milestone 3: Ready and LO3

- Track connected players and ready state.
- Support `.ready` and `.notready`.
- Start match when enough active players are ready.
- Implement LO3 through scheduled `sv_restart 1` commands.

QA scenario:

- Tool: multiple clients/bots and server console.
- Steps:
  1. Set `xmp_players_min 2` for fast testing.
  2. Connect two players and place them on T/CT teams.
  3. Each player runs `.ready`.
  4. Observe server console and chat announcements.
- Expected results:
  - Ready counter reaches 2/2.
  - Plugin schedules and executes three `sv_restart 1` commands when `xmp_lo3_enabled 1`.
  - State becomes `FirstHalf` after LO3.
  - Live config is executed.

### Milestone 4: Score tracking

- Intercept engine message stream.
- Track `TeamScore` updates for `TERRORIST` and `CT`.
- Treat score increments during live states as round completion.
- Enter halftime and finished/overtime based on configured round counts.

QA scenario:

- Tool: live server with message debug cvar if needed.
- Steps:
  1. Set `xmp_half_rounds 1` and `xmp_match_rounds 2` for fast testing.
  2. Start a match.
  3. Complete one round naturally.
  4. Complete the second round naturally.
- Expected results:
  - First score increment is detected from `TeamScore`.
  - After one round, state changes to `HalfTime`.
  - After second-half round, state changes to `Finished` or `Overtime` if tied and overtime is enabled.
  - `.score` reports tracked T/CT score consistently with scoreboard.

### Milestone 5: Admin controls and pause

- Implement `!start`, `!stop`, `!restart`, `!pause`, `!unpause`, `!swap`, `!score`.
- Implement simple pause via `pausable`, `mp_freezetime`, and scheduled continuation where supported.

QA scenario:

- Tool: admin client and server console.
- Steps:
  1. During live state, admin runs `!pause`.
  2. Wait for pause announcement and timer.
  3. Admin runs `!unpause`.
  4. Admin runs `!restart`, `!swap`, `!score`, and `!stop` in controlled test states.
- Expected results:
  - Pause state is announced and timer is visible in chat/HUD/center print.
  - `!unpause` clears pause state and restores relevant cvars.
  - `!restart` restarts current match state without corrupting score arrays.
  - `!swap` swaps tracked team labels or issues documented swap commands.
  - `!stop` returns state to warmup/disabled safely.

### Milestone 6: Documentation and verification

- Document install, build, cvars, commands, and known limitations.
- Verify compilation if HLSDK/Metamod headers are available.
- Otherwise provide exact missing dependency paths.

QA scenario:

- Tool: README review, shell build command, server smoke test.
- Steps:
  1. Follow README build instructions from a clean checkout.
  2. Follow README install instructions into a Xash3D CS server tree.
  3. Start server and run `meta list`.
  4. Run a two-player fast match with `xmp_players_min 2`, `xmp_half_rounds 1`, and `xmp_match_rounds 2`.
- Expected results:
  - README commands are sufficient or explicitly document required SDK environment variables.
  - Plugin loads through `plugins.ini`.
  - Fast match can start, enter halftime, and finish/overtime without crash.
  - Known limitations section covers inferred round events, side-swap uncertainty, and deferred damage stats.

## Initial Known Risks

1. Round-end detection is inferred from `TeamScore`, less precise than ReGameDLL `RoundEnd`.
2. Forced team switching may differ across Xash3D/CS builds; initial implementation may use client commands and manual fallback.
3. Damage stats require unavailable hooks and are intentionally deferred.
4. Menu support is intentionally deferred; chat commands are the MVP control surface.
5. SDK include paths may need local adjustment depending on installed Xash3D/HLSDK/Metamod headers.

## Definition of Done for First Implementation Pass

- Plan is saved in `.sisyphus/plans/`.
- Project files exist in `/Users/b4iterdev/XashMetaPUG`.
- Plugin code contains Metamod entry points and MVP scrim logic.
- Config package exists under `cstrike/addons/xashmetapug`.
- Build attempt has been run or blocked by clearly identified missing SDK paths.
