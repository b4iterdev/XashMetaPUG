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


if __name__ == "__main__":
    unittest.main()
