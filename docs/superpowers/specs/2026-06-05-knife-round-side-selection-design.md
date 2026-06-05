# Knife Round Side Selection Design

## Goal

Add a pre-LIVE knife round. The team that wins the knife round chooses whether to stay on the current side or swap sides before the real match goes LIVE. Side switching is blocked while the match is LIVE.

## Design

The match lifecycle gains two states before `FirstHalf`: `KnifeRound` and `SideSelection`. A normal `!start`, ready completion, or `xmp_forcestart` starts the knife round first when the next live state is `FirstHalf` and the current match has not already completed knife selection. The first `TeamScore` increment during `KnifeRound` determines the winner without adding to match scores.

After the knife round, the plugin enters `SideSelection` and announces that players on the winning team may type `.stay` or `.swap`. `.stay` starts the existing LO3 flow into `FirstHalf`. `.swap` swaps teams, then starts LO3 into `FirstHalf`.

`!swap` remains available before LIVE and at non-live control states, but it is denied in `FirstHalf`, `SecondHalf`, and `Overtime`. Player `jointeam` commands are also intercepted and denied during those LIVE states to prevent manual side switching.

## Test Strategy

Static behavior tests verify that the source contains the new states, winner-only `.stay` / `.swap` commands, pre-FirstHalf knife gating, non-scoring knife round handling, and live-state side-switch blocking. The C++ build verifies syntax and integration with the existing Metamod plugin code.
