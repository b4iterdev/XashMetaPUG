from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLUGIN_H = (ROOT / "src" / "plugin.h").read_text()
PLUGIN_CPP = (ROOT / "src" / "plugin.cpp").read_text()
WARMUP_CFG = (ROOT / "cstrike" / "addons" / "xashmetapug" / "cfg" / "warmup.cfg").read_text()
HALFTIME_CFG = (ROOT / "cstrike" / "addons" / "xashmetapug" / "cfg" / "halftime.cfg").read_text()


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
        self.assertIn('Schedule("knife_winner_transition", 0.0f, false', PLUGIN_CPP)
        self.assertIn("SetState(MatchState::SideSelection)", PLUGIN_CPP)
        self.assertLess(
            PLUGIN_CPP.index('Schedule("knife_winner_transition", 0.0f, false'),
            PLUGIN_CPP.index("SetState(MatchState::SideSelection)"),
        )

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

    def test_second_half_lo3_preserves_engine_score_baseline_and_resets_money(self):
        self.assertIn("const bool preservePlayerScores = ShouldPreservePlayerScores(pendingLiveState_)", PLUGIN_CPP)
        self.assertIn("lastObservedTScore_ = 0", PLUGIN_CPP)
        self.assertIn("lastObservedCTScore_ = 0", PLUGIN_CPP)
        self.assertIn("lastObservedTScore_ = terroristScore_", PLUGIN_CPP)
        self.assertIn("lastObservedCTScore_ = ctScore_", PLUGIN_CPP)
        self.assertIn("void ResetLivePlayerLoadout(int money)", PLUGIN_H)
        self.assertIn("SetPlayerMoneyNative(entity, money, false)", PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_startmoney %d\\n", money)', PLUGIN_CPP)
        self.assertIn('ServerCommand("sv_restart 1\\n")', PLUGIN_CPP)
        self.assertIn('Schedule("restore_scores"', PLUGIN_CPP)

    def test_second_half_lo3_resets_inventory_like_restart_without_restart(self):
        self.assertIn("void ResetLivePlayerInventory(edict_t *entity)", PLUGIN_H)
        self.assertIn("ResetLivePlayerInventory(entity)", PLUGIN_CPP)
        self.assertIn("player->m_iKevlar = ARMOR_NONE", PLUGIN_CPP)
        self.assertIn("entity->v.armorvalue = 0.0f", PLUGIN_CPP)
        self.assertIn("player->m_bHasPrimary = false", PLUGIN_CPP)
        self.assertIn("player->m_bHasC4 = false", PLUGIN_CPP)
        self.assertIn("player->m_bHasDefuser = false", PLUGIN_CPP)
        self.assertIn("std::fill(std::begin(player->m_rgAmmo), std::end(player->m_rgAmmo), 0)", PLUGIN_CPP)
        self.assertIn("StripPlayerWeaponsNative(entity)", PLUGIN_CPP)
        self.assertIn('GiveItemNative(entity, "weapon_knife")', PLUGIN_CPP)
        self.assertIn('GiveItemNative(entity, "weapon_glock18")', PLUGIN_CPP)
        self.assertIn('GiveItemNative(entity, "weapon_usp")', PLUGIN_CPP)

    def test_warmup_and_halftime_practice_rules_are_enforced(self):
        self.assertIn("ApplyPracticeStateRules()", PLUGIN_CPP)
        self.assertIn("IsPracticeState(state)", PLUGIN_CPP)
        self.assertIn("state == MatchState::Warmup || state == MatchState::HalfTime", PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_forcerespawn 1\\n")', PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_roundtime 60\\n")', PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_startmoney 16000\\n")', PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_give_player_c4 0\\n")', PLUGIN_CPP)
        self.assertIn('Schedule("practice_enforce", 1.0f, true', PLUGIN_CPP)
        self.assertIn("EnforcePracticePlayer(entity)", PLUGIN_CPP)
        self.assertIn("SetPlayerMoneyNative(entity, 16000, false)", PLUGIN_CPP)
        self.assertIn("RemovePlayerC4Native(entity)", PLUGIN_CPP)
        self.assertIn("player->m_bHasC4 = false", PLUGIN_CPP)
        self.assertIn("entity->v.weapons &= ~(1 << WEAPON_C4)", PLUGIN_CPP)

    def test_lo3_and_live_rules_restore_match_cvars(self):
        self.assertIn("ApplyLiveStateRules()", PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_forcerespawn 0\\n")', PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_roundtime 1.75\\n")', PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_startmoney 800\\n")', PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_give_player_c4 1\\n")', PLUGIN_CPP)
        self.assertIn('ServerCommand("mp_c4timer 35\\n")', PLUGIN_CPP)
        self.assertIn('CancelTask("practice_enforce")', PLUGIN_CPP)

    def test_practice_state_configs_match_enforced_values(self):
        for config in (WARMUP_CFG, HALFTIME_CFG):
            self.assertIn("mp_forcerespawn 1", config)
            self.assertIn("mp_roundtime 60", config)
            self.assertIn("mp_startmoney 16000", config)
            self.assertIn("mp_maxmoney 16000", config)
            self.assertIn("mp_give_player_c4 0", config)

    def test_knife_round_zeroes_money_and_strips_pistol_without_disabling_buy(self):
        self.assertIn("EnforceKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn("void EnforceKnifeRoundWeapons(bool announce = true)", PLUGIN_H)
        self.assertIn("RestoreKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn('"mp_startmoney 0\\n"', PLUGIN_CPP)
        self.assertIn('"mp_maxmoney 0\\n"', PLUGIN_CPP)
        self.assertNotIn('"mp_buytime 0\\n"', PLUGIN_CPP)
        self.assertNotIn('"sv_buy_status_override 3\\n"', PLUGIN_CPP)
        self.assertNotIn("Buying is disabled during the knife round", PLUGIN_CPP)

    def test_buy_buyequip_rebuy_commands_are_not_blocked_during_knife_round(self):
        self.assertNotIn('"buyequip"', PLUGIN_CPP)
        self.assertNotIn('"rebuy"', PLUGIN_CPP)
        self.assertNotIn('"cl_autobuy"', PLUGIN_CPP)
        self.assertNotIn('"cl_setautobuy"', PLUGIN_CPP)
        self.assertNotIn('"cl_rebuy"', PLUGIN_CPP)
        self.assertNotIn("IsKnifeRoundBlockBuy(state_)", PLUGIN_CPP)
        self.assertNotIn("buy disabled", PLUGIN_CPP)
        self.assertIn("bool IsKnifeRoundState(MatchState state) const", PLUGIN_H)
        self.assertIn("state_ == MatchState::KnifeRound", PLUGIN_CPP)

    def test_knife_round_strips_pistol_once_and_re_arms_knife_after_restart(self):
        self.assertIn("StripKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn("StripPlayerWeaponsNative(entity)", PLUGIN_CPP)
        self.assertIn('GiveItemNative(entity, "weapon_knife")', PLUGIN_CPP)
        self.assertIn('CreateNamedEntity("player_weaponstrip")', PLUGIN_CPP)
        self.assertIn('GiveItemNative(entity, "weapon_knife")', PLUGIN_CPP)
        self.assertIn("MDLL_Spawn", PLUGIN_CPP)
        self.assertIn("MDLL_Touch", PLUGIN_CPP)
        self.assertIn("MDLL_Use", PLUGIN_CPP)
        self.assertIn("StripKnifeRoundWeapons", PLUGIN_CPP)
        self.assertIn("Schedule(\"knife_strip\", 1.5f, false", PLUGIN_CPP)
        self.assertNotIn("Schedule(\"knife_strip\", 0.5f, true", PLUGIN_CPP)
        self.assertLess(
            PLUGIN_CPP.index("EnforceKnifeRoundWeapons()"),
            PLUGIN_CPP.index("StripKnifeRoundWeapons()"),
        )

    def test_knife_round_winner_stops_knife_enforcement_before_side_selection(self):
        self.assertIn("RestoreKnifeRoundWeapons()", PLUGIN_CPP)
        self.assertIn('Schedule("knife_winner_transition", 0.0f, false', PLUGIN_CPP)
        winner_idx = PLUGIN_CPP.index("void Plugin::HandleKnifeRoundScore")
        winner_body = PLUGIN_CPP[winner_idx:winner_idx + 1600]
        self.assertIn("RestoreKnifeRoundWeapons()", winner_body)
        self.assertIn('Schedule("knife_winner_transition", 0.0f, false', winner_body)
        self.assertIn("SetState(MatchState::SideSelection)", winner_body)
        self.assertLess(winner_body.index("RestoreKnifeRoundWeapons()"), winner_body.index("SetState(MatchState::SideSelection)"))

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

    def test_swap_teams_preassigns_random_model_to_skip_class_menu(self):
        self.assertIn("AssignRandomModelForTeam(entity, newTeam)", PLUGIN_CPP)
        self.assertIn("QueueRandomClassSelection(i, entity, newTeam)", PLUGIN_CPP)
        self.assertIn("void AssignRandomModelForTeam(edict_t *entity, Team team)", PLUGIN_H)
        self.assertIn("void QueueRandomClassSelection(int index, edict_t *entity, Team team)", PLUGIN_H)
        self.assertIn("void ForcePendingClassSelection(int index)", PLUGIN_H)
        self.assertIn("void ClearPendingClassSelection(int index)", PLUGIN_H)
        self.assertIn("int pendingClassSlot = 0", PLUGIN_H)
        self.assertIn("static constexpr ModelName kTModels[]", PLUGIN_CPP)
        self.assertIn("static constexpr ModelName kCTModels[]", PLUGIN_CPP)
        self.assertIn("MODEL_URBAN", PLUGIN_CPP)
        self.assertIn("MODEL_TERROR", PLUGIN_CPP)
        self.assertIn("MODEL_LEET", PLUGIN_CPP)
        self.assertIn("MODEL_ARCTIC", PLUGIN_CPP)
        self.assertIn("MODEL_GUERILLA", PLUGIN_CPP)
        self.assertIn("MODEL_MILITIA", PLUGIN_CPP)
        self.assertIn("MODEL_GSG9", PLUGIN_CPP)
        self.assertIn("MODEL_GIGN", PLUGIN_CPP)
        self.assertIn("MODEL_SAS", PLUGIN_CPP)
        self.assertIn("MODEL_SPETSNAZ", PLUGIN_CPP)
        self.assertIn("player->m_iModelName = kTModels[std::rand() % count]", PLUGIN_CPP)
        self.assertIn("player->m_iModelName = kCTModels[std::rand() % count]", PLUGIN_CPP)
        self.assertIn("std::srand(static_cast<unsigned>(std::time(nullptr)))", PLUGIN_CPP)
        self.assertIn('char classCommand[] = "joinclass %d\\n"', PLUGIN_CPP)
        self.assertIn("pendingClassSlot", PLUGIN_CPP)
        self.assertIn('message_.name == "VGUIMenu" || message_.name == "ShowMenu"', PLUGIN_CPP)
        self.assertIn('Schedule(Format("force_class_%d_a", index), 0.1f, false', PLUGIN_CPP)
        self.assertIn('Schedule(Format("force_class_%d_b", index), 0.5f, false', PLUGIN_CPP)
        self.assertIn('Schedule(Format("force_class_%d_c", index), 1.0f, false', PLUGIN_CPP)
        self.assertIn('Schedule(Format("clear_class_%d", index), 2.0f, false', PLUGIN_CPP)

    def test_second_half_lo3_restores_player_scores_across_sv_restart(self):
        self.assertIn("restoringScores_", PLUGIN_H)
        self.assertIn("Schedule(\"restore_scores\", 1.5f, false", PLUGIN_CPP)
        self.assertIn("restoringScores_ = true", PLUGIN_CPP)
        self.assertIn('Schedule("stop_restoring_scores", 5.0f, false', PLUGIN_CPP)
        self.assertIn("restoringScores_ = false", PLUGIN_CPP)
        self.assertIn("message_.name == \"ScoreInfo\" && restoringScores_", PLUGIN_CPP)
        self.assertIn("savedScoreInfo_[index][1]", PLUGIN_CPP)
        self.assertIn("savedScoreInfo_[index][2]", PLUGIN_CPP)
        self.assertIn("entity->v.frags = static_cast<float>(savedScoreInfo_[i][1])", PLUGIN_CPP)
        self.assertIn("player->m_iDeaths = savedScoreInfo_[i][2]", PLUGIN_CPP)
        self.assertIn("liveState == MatchState::SecondHalf", PLUGIN_CPP)

    def test_knife_round_disables_forcerespawn_without_periodic_knife_spam(self):
        self.assertIn("void ApplyKnifeRoundStateRules()", PLUGIN_H)
        self.assertIn("ApplyKnifeRoundStateRules()", PLUGIN_CPP)
        self.assertIn("state == MatchState::KnifeRound", PLUGIN_CPP)
        self.assertIn("state_ == MatchState::KnifeRound", PLUGIN_CPP)
        self.assertIn("knifeRoundWeaponsEnforced_ = true", PLUGIN_CPP)
        self.assertIn("mp_forcerespawn 0", PLUGIN_CPP)
        self.assertNotIn("knife_enforce", PLUGIN_CPP)
        self.assertNotIn("EnforceKnifeRoundWeapons(false)", PLUGIN_CPP)
        self.assertIn("EnforceKnifeRoundPlayerNative(entity)", PLUGIN_CPP)
        on_client_idx = PLUGIN_CPP.index("OnClientPutInServer(edict_t *entity)")
        on_client_body = PLUGIN_CPP[on_client_idx:on_client_idx + 600]
        self.assertIn("state_ == MatchState::KnifeRound", on_client_body)
        self.assertIn("EnforceKnifeRoundPlayerNative(entity)", on_client_body)


if __name__ == "__main__":
    unittest.main()
