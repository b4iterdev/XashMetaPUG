# Knife Round Side Selection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a knife round before first LIVE where the winning team chooses `.stay` or `.swap`, and block side switching during LIVE.

**Architecture:** Extend the existing `MatchState` lifecycle with `KnifeRound` and `SideSelection`. Track knife-round completion and winning side inside `Plugin`, reuse existing `TeamScore` handling for round winner detection, and reuse `SwapTeams()` before LO3. Add command gates for `.stay`, `.swap`, admin `!swap`, and raw `jointeam`.

**Tech Stack:** C++17 Metamod plugin, existing Makefile, Python static behavior tests.

---

### Task 1: Add failing static behavior tests

**Files:**
- Create: `tests/test_knife_round_static.py`

- [ ] **Step 1: Write tests that describe the feature**

```python
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN_H = (ROOT / "src" / "plugin.h").read_text()
PLUGIN_CPP = (ROOT / "src" / "plugin.cpp").read_text()


def test_match_states_include_knife_round_and_side_selection():
    assert "KnifeRound" in PLUGIN_H
    assert "SideSelection" in PLUGIN_H


def test_first_half_start_is_gated_by_knife_round():
    assert "StartKnifeRound()" in PLUGIN_CPP
    assert "!knifeRoundCompleted_" in PLUGIN_CPP
    assert "pendingLiveState_ == MatchState::FirstHalf" in PLUGIN_CPP


def test_knife_round_score_sets_winner_without_match_score_increment():
    assert "HandleKnifeRoundScore(team, score)" in PLUGIN_CPP
    assert "knifeWinner_ = winningTeam" in PLUGIN_CPP
    assert "SetState(MatchState::SideSelection)" in PLUGIN_CPP


def test_winning_players_choose_stay_or_swap():
    assert 'normalized == "stay"' in PLUGIN_CPP
    assert 'normalized == "swap"' in PLUGIN_CPP
    assert "HandleSideSelection(entity, false)" in PLUGIN_CPP
    assert "HandleSideSelection(entity, true)" in PLUGIN_CPP
    assert "players_[index].team != knifeWinner_" in PLUGIN_CPP


def test_side_switching_is_blocked_while_live():
    assert "IsLiveState(state_)" in PLUGIN_CPP
    assert 'strcasecmp(cmd, "jointeam") == 0' in PLUGIN_CPP
    assert 'normalized == "swap"' in PLUGIN_CPP
    assert "Side switching is disabled while LIVE" in PLUGIN_CPP
```

- [ ] **Step 2: Run tests to verify RED**

Run: `python3 -m unittest discover -s tests -p 'test_*.py'`
Expected: FAIL because `KnifeRound`, `SideSelection`, and handlers do not exist yet.

### Task 2: Implement knife round state and side selection

**Files:**
- Modify: `src/plugin.h`
- Modify: `src/plugin.cpp`

- [ ] **Step 1: Add states, helpers, and fields**

Add `KnifeRound` and `SideSelection` to `MatchState`; add declarations for `StartKnifeRound`, `HandleKnifeRoundScore`, `HandleSideSelection`, and `IsLiveState`; add fields `knifeRoundCompleted_`, `sideSelectionPending_`, and `knifeWinner_`.

- [ ] **Step 2: Gate first-half starts through knife round**

In `StartMatch`, if `pendingLiveState_ == MatchState::FirstHalf` and `knifeRoundCompleted_` is false, call `StartKnifeRound()` instead of `StartLO3()`.

- [ ] **Step 3: Detect knife winner**

In `HandleRoundScore`, route `KnifeRound` score increments to `HandleKnifeRoundScore`. That handler records the winning team, marks side selection pending, enters `SideSelection`, and announces `.stay` / `.swap`.

- [ ] **Step 4: Add player side commands**

In `DispatchPlayerCommand`, add `.stay` and `.swap`. Only players on `knifeWinner_` can choose. `.swap` calls `SwapTeams()` before starting LO3; `.stay` starts LO3 directly.

- [ ] **Step 5: Block live side switching**

Intercept raw `jointeam` during live states and block it. Block admin `!swap` during live states.

### Task 3: Verify behavior

**Files:**
- Test: `tests/test_knife_round_static.py`

- [ ] **Step 1: Run tests to verify GREEN**

Run: `python3 -m unittest discover -s tests -p 'test_*.py'`
Expected: PASS.

- [ ] **Step 2: Build plugin**

Run: `make`
Expected: Exit code 0 and updated build output.
