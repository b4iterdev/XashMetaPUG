# XashMetaPUG ReGameDLL Migration Plan

## Goal

Refactor XashMetaPUG from pure-Metamod message-inference to event-driven ReGameDLL hook chains, using the same SDK headers already bundled in the build tree. The server runs Velaron/ReGameDLL_CS, so the original "Metamod-only" constraint no longer applies.

## Background

The current plugin (1615 lines, 3 files) works by:
- Hooking `pfnMessageBegin`/`pfnWrite*`/`pfnMessageEnd` to intercept engine user messages
- Parsing `TeamScore` deltas to **infer** round wins
- Parsing `ScoreInfo`, `Money`, `TeamInfo`, `VGUIMenu`, `ShowMenu` to reconstruct player state
- Using `player_weaponstrip` entities + `MDLL_Spawn/Use` for weapon control
- Direct `CBasePlayer` member access for money/items/deaths

ReGameDLL exposes typed C++ hook chains that fire at the actual game events, eliminating all inference and most message interception.

## Reference: MatchBot Pattern

MatchBot (`/Users/b4iterdev/MatchBot/MatchBot/`) follows this pattern:

1. **`ReGameDLL_Init()`** — called from `Meta_Attach`, loads the game DLL module via `Sys_LoadModule`, gets the `IReGameApi` interface via factory, resolves version, stores globals, registers all hooks
2. **Hook functions** — each receives a `chain` pointer + typed params, calls `chain->callNext(...)` to chain, runs plugin logic before/after
3. **`ReGameDLL_Stop()`** — called from `Meta_Detach`, unregisters all hooks

## Prerequisites

- [ ] Confirm the target Xash3D server has Velaron/ReGameDLL_CS loaded as the game DLL (not vanilla `mp.dll`)
- [ ] Add `#include <regamedll_api.h>` and `#include <hookchains.h>` to the include path (already in SDK at `cssdk/dlls/`)
- [ ] No extra linking needed — ReGameDLL hooks are resolved at runtime via interface query, not compile-time linking

## Migration Strategy: Incremental, not rewrite

ReGameDLL hooks **add** functionality on top of Metamod. Metamod is still needed for:
- Plugin entry/exit (`Meta_Attach`/`Meta_Detach`)
- Engine function hooks (some message interception still needed for scoreboard)
- CVar registration
- `OnStartFrame` for task scheduling

The migration layers on ReGameDLL hooks without removing the Metamod infrastructure immediately. Each phase removes the corresponding Metamod hack once the ReGameDLL replacement is verified.

---

## Phase 0: ReGameDLL Initialization

**Files:** `src/regamedll.h` (new), `src/regamedll.cpp` (new)

### 0.1 Add ReGameDLL globals and init/shutdown

```cpp
// src/regamedll.h
#pragma once
#include <regamedll_api.h>

extern IReGameApi *g_ReGameApi;
extern const ReGameFuncs_t *g_ReGameFuncs;
extern IReGameHookchains *g_ReGameHookchains;

bool ReGameDLL_Init();
bool ReGameDLL_Stop();
```

```cpp
// src/regamedll.cpp - follows MatchBot pattern exactly:
// Sys_LoadModule(gamedll path from GET_GAME_INFO) 
// -> Sys_GetFactory 
// -> ifaceFactory(VRE_GAMEDLL_API_VERSION) -> IReGameApi
// -> g_ReGameApi->GetHookchains() -> register hooks
// -> g_ReGameApi->GetFuncs() -> helper functions
```

### 0.2 Wire into Meta_Attach

```cpp
// metamod_api.cpp Meta_Attach:
ReGameDLL_Init();  // after Metamod globals are set up
```

### 0.3 Wire into Meta_Detach

```cpp
// metamod_api.cpp Meta_Detach:
ReGameDLL_Stop();
```

### 0.4 Add include dirs to Makefile

The SDK already has `regamedll_api.h` at `$(SDK_ROOT)/cssdk/dlls/` which is already in the include path. Also add `rehlds` API headers if needed for `Sys_LoadModule`/`Sys_GetFactory`:

```
INCLUDES += -I$(SDK_ROOT)/rehlds  # if Sys_LoadModule isn't in metamod SDK
```

**Check:** MatchBot's makefile uses `-I$(CSSDK)/dlls` and `-I$(CSSDK)/engine`, which cover regamedll_api.h and rehlds headers. XashMetaPUG's Makefile already has both paths via `cssdk/dlls` and `cssdk/engine`.

`Sys_LoadModule` and `Sys_GetFactory` are defined in the Metamod SDK (`sys_shared.h`/`interface.h`). MatchBot includes them via:
```cpp
#include "sys_shared.cpp"  // in ReAPI.cpp
#include "interface.cpp"     // in ReAPI.cpp
```

Need to verify these are available or provide standalone wrappers.

---

## Phase 1: Round-End Detection (highest impact)

**Removes:** ~80 lines of fragile `TeamScore` delta inference

### 1.1 Register the hook

```cpp
g_ReGameHookchains->RoundEnd()->registerHook(OnRoundEnd);
```

### 1.2 Implement the callback

```cpp
bool OnRoundEnd(IReGameHook_RoundEnd *chain, int winStatus, ScenarioEventEndRound event, float tmDelay)
{
    // winStatus: WINSTATUS_TERRORISTS (1), WINSTATUS_CTS (2)
    // event: ROUND_END_DEFAULT, ROUND_END_TARGET_BOMBED, etc.
    
    Team winner = (winStatus == WINSTATUS_TERRORISTS) ? Team::Terrorist : Team::CounterTerrorist;
    GetPlugin().OnRoundEnd(winner);
    
    return chain->callNext(winStatus, event, tmDelay);
}
```

### 1.3 Add `Plugin::OnRoundEnd(Team winner)`

- Increment match score
- Check win conditions (same as `EvaluateMatchProgress` but triggered 100% reliably)
- Handle knife round winner detection properly

### 1.4 What to remove from Phase 1

- [ ] The entire `TeamScore` message interception code path in `OnMessageBegin`/`OnMessageEnd`
- [ ] `HandleRoundScore()` and `HandleKnifeRoundScore()`
- [ ] `lastObservedTScore_` / `lastObservedCTScore_` state
- [ ] `ShouldRewriteTeamScoreMessage()` logic
- [ ] `SendTeamScore()` / `SetDisplayedTeamScore()` / `SyncDisplayedTeamScoresFromMatchScores()` — MAYBE keep for scoreboard manipulation

**Trade-off:** If you still want to overlay custom scores on the scoreboard (e.g., show T/CT differently during knife round), keep the `TeamScore` message interception as a *display* layer, but strip the *inference* logic. The round outcome now comes from the hook, not from parsing score deltas.

---

## Phase 2: Spawn Control & Equipment (high impact)

**Removes:** player_weaponstrip hack, manual money/C4/armor manipulation

### 2.1 Hook CBasePlayer_OnSpawnEquip

```cpp
g_ReGameHookchains->CBasePlayer_OnSpawnEquip()->registerHook(OnSpawnEquip);
```

```cpp
void OnSpawnEquip(IReGameHook_CBasePlayer_OnSpawnEquip *chain, CBasePlayer *player, bool addDefault, bool equipGame)
{
    auto &p = GetPlugin();
    if (p.GetState() == MatchState::KnifeRound) {
        // Block default equipment, just give knife
        chain->callNext(player, addDefault, equipGame);  // or skip entirely
        // Then strip everything and give knife via CCSPlayer API
        return;
    }
    chain->callNext(player, addDefault, equipGame);
}
```

### 2.2 Use CCSPlayer API for equipment control

Instead of:
```cpp
player->m_iAccount = 0;
g_engfuncs.pfnMessageBegin(MSG_ONE, moneyMsgId, nullptr, entity);
g_engfuncs.pfnWriteLong(0);
g_engfuncs.pfnMessageEnd();
```

Use:
```cpp
CCSPlayer *csPlayer = (CCSPlayer *)player;
csPlayer->AddAccount(amount, RT_NONE, false);
```

Instead of:
```cpp
edict_t *stripper = CreateNamedEntity("player_weaponstrip");
MDLL_Spawn(stripper);
MDLL_Use(stripper, entity);
```

Use:
```cpp
CCSPlayer *csPlayer = (CCSPlayer *)player;
csPlayer->RemoveAllItems(true);  // true = keep suit
```

### 2.3 What to remove from Phase 2

- [ ] `StripPlayerWeaponsNative()`
- [ ] `GiveItemNative()`
- [ ] `SetPlayerMoneyNative()`
- [ ] `RemovePlayerC4Native()`
- [ ] `EnforceKnifeRoundPlayerNative()` (simplify to use CCSPlayer API)
- [ ] `EnforcePracticePlayer()` (simplify)
- [ ] `ResetLivePlayerInventory()` (simplify)
- [ ] `CreateNamedEntity()` (maybe keep if used elsewhere)

---

## Phase 3: Team & Menu Control (medium impact)

**Removes:** jointeam intercept + VGUIMenu/ShowMenu suppression + random class timer hackery

### 3.1 Hook HandleMenu_ChooseTeam

```cpp
g_ReGameHookchains->HandleMenu_ChooseTeam()->registerHook(OnChooseTeam);
```

```cpp
BOOL OnChooseTeam(IReGameHook_HandleMenu_ChooseTeam *chain, CBasePlayer *player, int slot)
{
    auto &p = GetPlugin();
    if (p.IsSideSwitchBlocked(p.GetState())) {
        // Tell player they can't switch, return MRES_SUPERCEDE equivalent
        return TRUE;  // Suppress the menu
    }
    return chain->callNext(player, slot);
}
```

### 3.2 Hook CBasePlayer_SwitchTeam

```cpp
g_ReGameHookchains->CBasePlayer_SwitchTeam()->registerHook(OnSwitchTeam);
```

### 3.3 Use CCSPlayer API for team operations

Instead of:
```cpp
g_engfuncs.pfnClientCommand(entity, "jointeam %d\n", targetTeam);
```
Use:
```cpp
CCSPlayer *csPlayer = (CCSPlayer *)player;
csPlayer->JoinTeam(targetTeam == 1 ? TeamName::TERRORIST : TeamName::CT);
// or
csPlayer->SwitchTeam();
```

### 3.4 Simplify model assignment

Instead of direct `m_iModelName` assignment:
```cpp
CCSPlayer *csPlayer = (CCSPlayer *)player;
csPlayer->SetPlayerModelEx("urban");
```

### 3.5 What to remove from Phase 3

- [ ] `QueueRandomClassSelection()` / `ForcePendingClassSelection()` / `ClearPendingClassSelection()` — entire class menu hack
- [ ] `AssignRandomModelForTeam()` — replace with `SetPlayerModelEx`
- [ ] VGUIMenu/ShowMenu interception in `OnMessageBegin` — unless still needed for other purposes
- [ ] `pendingClassSlot` state in `PlayerInfo`

---

## Phase 4: Damage Stats (new capability)

**Unlocks:** the deferred "Full damage stats" feature

### 4.1 Hook CBasePlayer_TakeDamage

```cpp
g_ReGameHookchains->CBasePlayer_TakeDamage()->registerHook(OnTakeDamage);
```

Track: attacker, victim, weapon, damage amount, hitgroup → per-round damage stats

### 4.2 Hook CSGameRules_PlayerKilled

```cpp
g_ReGameHookchains->CSGameRules_PlayerKilled()->registerHook(OnPlayerKilled);
```

Track: killer, victim, weapon, headshot flag → kill/death tally

### 4.3 Hook CSGameRules_SendDeathMessage

```cpp
g_ReGameHookchains->CSGameRules_SendDeathMessage()->registerHook(OnDeathMessage);
```

Capture the exact death message for .status display.

---

## Phase 5: Polish & Cleanup (low impact)

### 5.1 Remove Message Interception That's No Longer Needed

After Phases 1-4:
- `TeamScore` interception → round inference gone, keep only for display overlay if needed
- `Money` interception → replace with `AddAccount` hook tracking
- `ScoreInfo` interception → mostly replaced by proper hooks
- `TeamInfo` interception → replaced by `SwitchTeam`/`JoinTeam` hooks
- `VGUIMenu` / `ShowMenu` interception → replaced by menu hooks
- `TextMsg` "#Game_Commencing" → replaced by `CSGameRules_RestartRound` hook

What to **keep** of message interception:
- If scoreboard display overlay is still needed, keep minimal `TeamScore` rewriting
- Otherwise, the entire `MessageCapture` struct and all `OnWrite*` methods can be removed

### 5.2 Simplify PlayerInfo struct

After removing: `pendingClassSlot`, reduced need for `scoreInfoValues`

### 5.3 Convert client command blocking

Current: Block `jointeam` in `OnClientCommand` via `IsSideSwitchBlocked`
New: Block in `HandleMenu_ChooseTeam` hook + `CBasePlayer_CanSwitchTeam` hook

```cpp
g_ReGameHookchains->CBasePlayer_CanSwitchTeam()->registerHook(OnCanSwitchTeam);
```

---

## File Manifest

### New files
| File | Purpose |
|---|---|
| `src/regamedll.h` | ReGameDLL globals + hook function declarations |
| `src/regamedll.cpp` | Init/shutdown + all hook callback implementations |

### Modified files
| File | Changes |
|---|---|
| `src/metamod_api.cpp` | Add `ReGameDLL_Init()` and `ReGameDLL_Stop()` calls |
| `src/plugin.h` | Add new methods (`OnRoundEnd`, etc.), remove fields (`lastObserved*Score_`, `pendingClassSlot`) |
| `src/plugin.cpp` | Replace inference logic with hook-driven events, use CCSPlayer API |
| `Makefile` | No changes needed (headers already in include path) |

---

## Migration Order (Recommended)

```
Phase 0: Init/shutdown skeleton → builds, loads, logs "ReGameDLL initialized"
Phase 1: RoundEnd hook → verify match progression is correct
Phase 2: Spawn/equipment hooks → verify knife round works
Phase 3: Team/menu hooks → verify side selection, team switching
Phase 4: Damage hooks → verify stats tracking
Phase 5: Cleanup → remove unused message interception, simplify structs
```

Each phase is independently testable. The plugin continues to work with both old and new code paths during transition.

---

## Risk Assessment

| Risk | Mitigation |
|---|---|
| ReGameDLL API version mismatch | Version check in init, graceful fallback or abort |
| Server has ReGameDLL but with different hook behavior | The hook chain pattern (`chain->callNext`) preserves original behavior; hooks only ADD logic |
| ReGameDLL not loaded on server | `ReGameDLL_Init()` returns false, plugin falls back to pure Metamod message inference (keep old code paths as fallback) |
| `Sys_LoadModule`/`Sys_GetFactory` not in Metamod SDK | Extract from MatchBot's `sys_shared.cpp` + `interface.cpp` includes |
| ARM64 compatibility of Sys_LoadModule | MatchBot's Makefile builds for aarch64 and uses the same pattern; verified working |

## Definition of Done

- [ ] Plugin builds and loads on Xash3D with ReGameDLL
- [ ] `RoundEnd` hook fires correctly, match progression is reliable (no more false positives from TeamScore inference)
- [ ] Knife round equipment is enforced via `OnSpawnEquip` hook (no entity hacks)
- [ ] Side selection works via `HandleMenu_ChooseTeam` hook (no timer-based class menu workarounds)
- [ ] Damage stats are tracked per round (new capability)
- [ ] Old message-interception code is removed or conditional
- [ ] Plugin still works on pure Metamod if ReGameDLL is absent (optional — depends on whether fallback is desired)
