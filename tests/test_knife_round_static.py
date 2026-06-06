from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLUGIN_H = (ROOT / "src" / "plugin.h").read_text()
PLUGIN_CPP = (ROOT / "src" / "plugin.cpp").read_text()


class KnifeRoundStaticTests(unittest.TestCase):
    def test_match_states_include_knife_round_and_side_selection(self):
        self.assertIn("KnifeRound", PLUGIN_H)
        self.assertIn("SideSelection", PLUGIN_H)

    def test_first_half_start_is_gated_by_knife_round(self):
        self.assertIn("StartKnifeRound()", PLUGIN_CPP)
        self.assertIn("!knifeRoundCompleted_", PLUGIN_CPP)
        self.assertIn("pendingLiveState_ == MatchState::FirstHalf", PLUGIN_CPP)

    def test_knife_round_score_sets_winner_without_match_score_increment(self):
        self.assertIn("HandleKnifeRoundScore(team, score)", PLUGIN_CPP)
        self.assertIn("knifeWinner_ = winningTeam", PLUGIN_CPP)
        self.assertIn("SetState(MatchState::SideSelection)", PLUGIN_CPP)

    def test_winning_players_choose_stay_or_swap(self):
        self.assertIn('normalized == "stay"', PLUGIN_CPP)
        self.assertIn('normalized == "swap"', PLUGIN_CPP)
        self.assertIn("HandleSideSelection(entity, false)", PLUGIN_CPP)
        self.assertIn("HandleSideSelection(entity, true)", PLUGIN_CPP)
        self.assertIn("players_[index].team != knifeWinner_", PLUGIN_CPP)

    def test_side_switching_is_blocked_while_live(self):
        self.assertIn("IsLiveState(state_)", PLUGIN_CPP)
        self.assertIn('strcasecmp(cmd, "jointeam") == 0', PLUGIN_CPP)
        self.assertIn('normalized == "swap"', PLUGIN_CPP)
        self.assertIn("Side switching is disabled while LIVE", PLUGIN_CPP)

    def test_side_switching_is_blocked_during_knife_and_selection(self):
        self.assertIn("IsSideSwitchBlocked(state_)", PLUGIN_CPP)
        self.assertIn("state == MatchState::KnifeRound", PLUGIN_CPP)
        self.assertIn("state == MatchState::SideSelection", PLUGIN_CPP)

    def test_finished_match_start_clears_knife_flags_for_fresh_knife_round(self):
        self.assertIn("state_ == MatchState::Finished", PLUGIN_CPP)
        self.assertIn("ResetMatch(true)", PLUGIN_CPP)
        self.assertIn("knifeRoundCompleted_ = false", PLUGIN_CPP)

    def test_overtime_first_to_uses_overtime_local_score_delta(self):
        self.assertIn("overtimeTerroristStartScore_", PLUGIN_H)
        self.assertIn("overtimeCTStartScore_", PLUGIN_H)
        self.assertIn("terroristScore_ - overtimeTerroristStartScore_", PLUGIN_CPP)
        self.assertIn("ctScore_ - overtimeCTStartScore_", PLUGIN_CPP)

    def test_halftime_swaps_scores_when_sides_swap(self):
        self.assertIn("SwapSideScores()", PLUGIN_CPP)
        self.assertIn("std::swap(terroristScore_, ctScore_)", PLUGIN_CPP)
        self.assertIn("std::swap(lastObservedTScore_, lastObservedCTScore_)", PLUGIN_CPP)
        self.assertLess(PLUGIN_CPP.index("SwapSideScores()"), PLUGIN_CPP.index("SwapTeams();\n    StartReady();"))

    def test_team_score_messages_are_rewritten_and_resent(self):
        self.assertIn("ShouldRewriteTeamScoreMessage()", PLUGIN_CPP)
        self.assertIn("SendTeamScore(team)", PLUGIN_CPP)
        self.assertIn("TeamScoreMessageId()", PLUGIN_CPP)
        self.assertIn("MRES_SUPERCEDE", (ROOT / "src" / "metamod_api.cpp").read_text())
        self.assertIn("RETURN_META(xmp::GetPlugin().OnMessageEnd() ? MRES_SUPERCEDE : MRES_IGNORED)", (ROOT / "src" / "metamod_api.cpp").read_text())

    def test_scoreboard_display_scores_are_managed_explicitly(self):
        self.assertIn("displayedTerroristScore_", PLUGIN_H)
        self.assertIn("displayedCTScore_", PLUGIN_H)
        self.assertIn("SetDisplayedTeamScore(Team team, int score, bool resend)", PLUGIN_H)
        self.assertIn("SetDisplayedTeamScores(int terroristScore, int ctScore, bool resend)", PLUGIN_H)
        self.assertIn("SyncDisplayedTeamScoresFromMatchScores(bool resend)", PLUGIN_H)
        self.assertIn("DisplayedTeamScore(team)", PLUGIN_CPP)
        self.assertIn("SetDisplayedTeamScore(team,", PLUGIN_CPP)
        self.assertIn("SyncDisplayedTeamScoresFromMatchScores(true)", PLUGIN_CPP)
        self.assertIn("g_engfuncs.pfnWriteShort(DisplayedTeamScore(team))", PLUGIN_CPP)

    def test_score_info_is_cached_and_replayed_after_second_half_swap(self):
        self.assertIn("scoreInfoValues", PLUGIN_H)
        self.assertIn("CacheScoreInfo()", PLUGIN_CPP)
        self.assertIn("ReplayAllScoreInfo()", PLUGIN_CPP)
        self.assertIn("ScoreInfoMessageId()", PLUGIN_CPP)

    def test_second_half_lo3_does_not_sv_restart(self):
        self.assertIn("ShouldPreservePlayerScores(liveState)", PLUGIN_CPP)
        self.assertIn("ShouldPreservePlayerScores(pendingLiveState_)", PLUGIN_CPP)
        self.assertIn("liveState == MatchState::SecondHalf", PLUGIN_CPP)
        self.assertIn("if (!preservePlayerScores)", PLUGIN_CPP)

    def test_knife_round_disables_buy_money_and_strips_pistol(self):
        self.assertIn("EnforceKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn("RestoreKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn('"mp_buytime 0\\n"', PLUGIN_CPP)
        self.assertIn('"sv_buy_status_override 3\\n"', PLUGIN_CPP)
        self.assertIn('"mp_startmoney 0\\n"', PLUGIN_CPP)
        self.assertIn('"mp_maxmoney 0\\n"', PLUGIN_CPP)
        self.assertIn('"cl_autobuy \\\"\\\"\\n"', PLUGIN_CPP)
        self.assertIn('"cl_setautobuy \\\"\\\"\\n"', PLUGIN_CPP)
        self.assertIn('"cl_rebuy \\\"\\\"\\n"', PLUGIN_CPP)

    def test_buy_buyequip_rebuy_commands_are_blocked_during_knife_round(self):
        self.assertIn('"buy"', PLUGIN_CPP)
        self.assertIn('"buyequip"', PLUGIN_CPP)
        self.assertIn('"rebuy"', PLUGIN_CPP)
        self.assertIn('"cl_autobuy"', PLUGIN_CPP)
        self.assertIn('"cl_setautobuy"', PLUGIN_CPP)
        self.assertIn('"cl_rebuy"', PLUGIN_CPP)
        self.assertIn("IsKnifeRoundBlockBuy(state_)", PLUGIN_CPP)
        self.assertIn("bool IsKnifeRoundState(MatchState state) const", PLUGIN_H)
        self.assertIn("IsKnifeRoundState(state)", PLUGIN_CPP)

    def test_knife_round_strips_pistol_and_re_arms_knife_via_drop_loop(self):
        self.assertIn("StripKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn("StripPlayerWeaponsNative(entity)", PLUGIN_CPP)
        self.assertIn('GiveItemNative(entity, "weapon_knife")', PLUGIN_CPP)
        self.assertIn('CreateNamedEntity("player_weaponstrip")', PLUGIN_CPP)
        self.assertIn('GiveItemNative(entity, "weapon_knife")', PLUGIN_CPP)
        self.assertIn("MDLL_Spawn", PLUGIN_CPP)
        self.assertIn("MDLL_Touch", PLUGIN_CPP)
        self.assertIn("MDLL_Use", PLUGIN_CPP)
        self.assertIn("StripKnifeRoundWeapons", PLUGIN_CPP)
        self.assertIn("Schedule(\"knife_strip\"", PLUGIN_CPP)
        self.assertLess(
            PLUGIN_CPP.index("EnforceKnifeRoundWeapons()"),
            PLUGIN_CPP.index("StripKnifeRoundWeapons()"),
        )

    def test_knife_round_weapon_state_is_restored_when_match_resets(self):
        self.assertIn("RestoreKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn("ResetMatch(true)", PLUGIN_CPP)
        self.assertIn("ResetMatch(false)", PLUGIN_CPP)
        self.assertIn("OnServerDeactivate", PLUGIN_CPP)
        self.assertLess(
            PLUGIN_CPP.index("ResetMatch(false)"),
            PLUGIN_CPP.index("RestoreKnifeRoundWeapons()"),
        )

    def test_knife_round_money_is_zeroed_with_native_pdata_and_money_message(self):
        self.assertIn("SetPlayerMoneyNative(entity, 0, false)", PLUGIN_CPP)
        self.assertIn("CBasePlayer::Instance(entity)", PLUGIN_CPP)
        self.assertIn("player->m_iAccount = money", PLUGIN_CPP)
        self.assertIn('GET_USER_MSG_ID(PLID, "Money", nullptr)', PLUGIN_CPP)
        self.assertIn("pfnWriteLong(money)", PLUGIN_CPP)
        self.assertIn("pfnWriteByte(flash ? 1 : 0)", PLUGIN_CPP)
        self.assertIn("int MoneyMessageId()", PLUGIN_H)


if __name__ == "__main__":
    unittest.main()
