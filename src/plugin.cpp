#include "plugin.h"

#ifndef __linux__
#define __linux__
#endif
#ifndef _vsnprintf
#define _vsnprintf vsnprintf
#endif

#include <cbase.h>
#include <player.h>
#include <regamedll_const.h>



namespace xmp {

Plugin &GetPlugin()
{
    static Plugin plugin;
    return plugin;
}

void Plugin::OnMetaAttach()
{
    RegisterCvars();
    LoadAdmins();
    g_engfuncs.pfnAddServerCommand(const_cast<char *>("xmp_forcestart"), []() {
        GetPlugin().ForceStartFromServer();
    });
    Log("XashMetaPUG attached");
}

void Plugin::ForceStartFromServer()
{
    if (CvarInt(cvars_.enabled) <= 0) {
        Log("xmp_forcestart ignored because xmp_enabled is 0");
        return;
    }

    Log("xmp_forcestart executed from server console");
    StartMatch(true);
}

void Plugin::OnServerActivate()
{
    LoadAdmins();
    ResetMatch(true);
    SetState(CvarInt(cvars_.enabled) ? MatchState::Warmup : MatchState::Disabled);
}

void Plugin::OnServerDeactivate()
{
    ClearTasks();
    ResetMatch(false);
}

void Plugin::OnStartFrame()
{
    const float now = gpGlobals ? gpGlobals->time : 0.0f;
    std::vector<std::function<void()>> dueCallbacks;

    for (auto it = tasks_.begin(); it != tasks_.end();) {
        if (now >= it->executeAt) {
            dueCallbacks.push_back(it->callback);
            if (it->repeat) {
                it->executeAt = now + it->interval;
                ++it;
            } else {
                it = tasks_.erase(it);
            }
        } else {
            ++it;
        }
    }

    for (const auto &callback : dueCallbacks) {
        if (callback) {
            callback();
        }
    }
}

bool Plugin::OnClientConnect(edict_t *entity, const char *name)
{
    const int index = PlayerIndex(entity);
    Log("OnClientConnect: entity=%p, name=%s, index=%d", entity, name ? name : "null", index);
    if (index <= 0 || index > kMaxClients) {
        Log("OnClientConnect: invalid index %d", index);
        return true;
    }

    PlayerInfo &player = players_[index];
    player.connected = true;
    player.ready = false;
    player.name = (name && name[0] != '\0') ? name : Format("player%d", index);
    player.authId.clear();
    player.admin = false;
    Log("OnClientConnect: player %d (%s) connected", index, player.name.c_str());
    return true;
}

void Plugin::OnClientPutInServer(edict_t *entity)
{
    Log("OnClientPutInServer: entity=%p, index=%d", entity, PlayerIndex(entity));
    UpdatePlayer(entity);
    if (IsPracticeState(state_)) {
        EnforcePracticePlayer(entity);
    }
    if (state_ == MatchState::WaitingReady || state_ == MatchState::HalfTime) {
        Say(entity, "[XMP] Type .ready when ready.\n");
    }
}

void Plugin::OnClientDisconnect(edict_t *entity)
{
    const int index = PlayerIndex(entity);
    if (IsConnectedPlayerIndex(index)) {
        players_[index] = PlayerInfo{};
    }
}

bool Plugin::OnClientCommand(edict_t *entity)
{
    const char *cmd = g_engfuncs.pfnCmd_Argv(0);
    if (!cmd || !*cmd) {
        return false;
    }

    if (strcasecmp(cmd, "jointeam") == 0 && IsSideSwitchBlocked(state_)) {
        Say(entity, "[XMP] Side switching is disabled while LIVE.\n");
        return true;
    }

    if (strcasecmp(cmd, "say") == 0 || strcasecmp(cmd, "say_team") == 0) {
        const char *args = g_engfuncs.pfnCmd_Args();
        if (!args) {
            return false;
        }
        std::string raw(args);
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }
        return DispatchCommand(entity, raw);
    }

    return DispatchCommand(entity, cmd);
}

bool Plugin::OnMessageBegin(int destination, int type, const float *origin, edict_t *entity)
{
    message_ = MessageCapture{};
    message_.destination = destination;
    message_.type = type;
    message_.entity = entity;
    suppressCurrentMessage_ = false;
    int size = 0;
    const char *name = gpMetaUtilFuncs ? GET_USER_MSG_NAME(PLID, type, &size) : nullptr;
    if (name) {
        message_.name = name;
    }

    suppressCurrentMessage_ = message_.name == "TeamScore" && ShouldRewriteTeamScoreMessage() && !replayingScoreMessages_;
    return suppressCurrentMessage_;
}

bool Plugin::OnWriteByte(int value) { message_.numbers.push_back(value); return suppressCurrentMessage_; }
bool Plugin::OnWriteChar(int value) { message_.numbers.push_back(value); return suppressCurrentMessage_; }
bool Plugin::OnWriteShort(int value) { message_.numbers.push_back(value); return suppressCurrentMessage_; }
bool Plugin::OnWriteLong(int value) { message_.numbers.push_back(value); return suppressCurrentMessage_; }
bool Plugin::OnWriteString(const char *value) {
    message_.strings.emplace_back(value ? value : "");
    if (message_.name == "TextMsg" && value && strstr(value, "#Game_Commencing")) {
        if (state_ == MatchState::Disabled) {
            Log("Detected Game Commencing from Disabled, resetting to Warmup.");
            ResetMatch(true);
            SetState(MatchState::Warmup);
        } else {
            Log("Detected Game Commencing while in state %s, ignoring (mid-match/round).", StateName(state_));
        }
    }
    return suppressCurrentMessage_;
}

bool Plugin::OnMessageEnd()
{
    const bool suppressMessage = suppressCurrentMessage_;
    if (CvarInt(cvars_.debugMessages) > 0 && !message_.name.empty()) {
        Log("msg=%s strings=%zu nums=%zu", message_.name.c_str(), message_.strings.size(), message_.numbers.size());
    }

    if (message_.name == "TextMsg") {
        suppressCurrentMessage_ = false;
        return suppressMessage;
    }

    if (message_.name == "TeamScore" && !message_.strings.empty() && !message_.numbers.empty()) {
        const Team team = ParseTeamName(message_.strings[0]);
        HandleRoundScore(team, message_.numbers[0]);
        if (suppressMessage) {
            SendTeamScore(team);
        }
    } else if (message_.name == "ScoreInfo" && !message_.numbers.empty()) {
        CacheScoreInfo();
    } else if (message_.name == "TeamInfo" && !message_.strings.empty() && !message_.numbers.empty()) {
        const int index = message_.numbers[0];
        if (IsConnectedPlayerIndex(index)) {
            const Team prev = players_[index].team;
            players_[index].team = ParseTeamName(message_.strings[0]);
            if (CvarInt(cvars_.debugMessages) > 0 && prev != players_[index].team) {
                Log("TeamInfo: player %d (%s) %s -> %s (raw=%s)",
                    index, players_[index].name.c_str(),
                    TeamName(prev), TeamName(players_[index].team),
                    message_.strings[0].c_str());
            }
        }
    }
    suppressCurrentMessage_ = false;
    return suppressMessage;
}

void Plugin::RegisterCvars()
{
    RegisterCvar(cvars_.enabled, "xmp_enabled", "1");
    RegisterCvar(cvars_.adminPrefix, "xmp_admin_prefix", "!");
    RegisterCvar(cvars_.playerPrefix, "xmp_player_prefix", ".");
    RegisterCvar(cvars_.playersMin, "xmp_players_min", "10");
    RegisterCvar(cvars_.playersMax, "xmp_players_max", "10");
    RegisterCvar(cvars_.readyType, "xmp_ready_type", "1");
    RegisterCvar(cvars_.readyTime, "xmp_ready_time", "60");
    RegisterCvar(cvars_.matchRounds, "xmp_match_rounds", "30");
    RegisterCvar(cvars_.halfRounds, "xmp_half_rounds", "15");
    RegisterCvar(cvars_.firstTo, "xmp_first_to", "16");
    RegisterCvar(cvars_.overtimeEnabled, "xmp_overtime_enabled", "1");
    RegisterCvar(cvars_.overtimeRounds, "xmp_overtime_rounds", "6");
    RegisterCvar(cvars_.overtimeFirstTo, "xmp_overtime_first_to", "4");
    RegisterCvar(cvars_.lo3Enabled, "xmp_lo3_enabled", "1");
    RegisterCvar(cvars_.pauseTime, "xmp_pause_time", "60");
    RegisterCvar(cvars_.votePercent, "xmp_vote_percent", "0.70");
    RegisterCvar(cvars_.cfgWarmup, "xmp_cfg_warmup", "addons/xashmetapug/cfg/warmup.cfg");
    RegisterCvar(cvars_.cfgLive, "xmp_cfg_live", "addons/xashmetapug/cfg/live.cfg");
    RegisterCvar(cvars_.cfgHalftime, "xmp_cfg_halftime", "addons/xashmetapug/cfg/halftime.cfg");
    RegisterCvar(cvars_.cfgOvertime, "xmp_cfg_overtime", "addons/xashmetapug/cfg/overtime.cfg");
    RegisterCvar(cvars_.cfgEnd, "xmp_cfg_end", "addons/xashmetapug/cfg/end.cfg");
    RegisterCvar(cvars_.debugMessages, "xmp_debug_messages", "0");
}

void Plugin::RegisterCvar(cvar_t &cvar, const char *name, const char *value, int flags)
{
    cvar.name = const_cast<char *>(name);
    cvar.string = const_cast<char *>(value);
    cvar.flags = flags;
    cvar.value = static_cast<float>(atof(value));
    g_engfuncs.pfnCVarRegister(&cvar);
}

void Plugin::LoadAdmins()
{
    admins_.clear();
    std::ifstream file("addons/xashmetapug/users.txt");
    if (!file.is_open()) {
        file.open("cstrike/addons/xashmetapug/users.txt");
    }
    std::string line;
    while (std::getline(file, line)) {
        const auto comment = line.find("//");
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        std::istringstream stream(line);
        std::string auth;
        if (stream >> auth && !auth.empty()) {
            admins_.insert(auth);
        }
    }
}

void Plugin::ResetMatch(bool keepWarmup)
{
    ClearTasks();
    ServerCommand("pausable 0\n");
    RestoreKnifeRoundWeapons();
    for (auto &player : players_) {
        player.ready = false;
    }
    lo3Step_ = 0;
    halfRoundCount_ = 0;
    totalRoundCount_ = 0;
    overtimeRoundCount_ = 0;
    terroristScore_ = 0;
    ctScore_ = 0;
    displayedTerroristScore_ = 0;
    displayedCTScore_ = 0;
    overtimeTerroristStartScore_ = 0;
    overtimeCTStartScore_ = 0;
    lastObservedTScore_ = 0;
    lastObservedCTScore_ = 0;
    paused_ = false;
    restarting_ = false;
    syncingScoreboard_ = false;
    knifeRoundCompleted_ = false;
    sideSelectionPending_ = false;
    knifeWinner_ = Team::Unknown;
    state_ = keepWarmup ? MatchState::Warmup : MatchState::Disabled;
}

void Plugin::SetState(MatchState next)
{
    state_ = next;
    Broadcast("[XMP] State: %s\n", StateName(state_));
    ExecuteStateConfig(state_);
    ApplyStateRules(state_);
}

void Plugin::ExecuteStateConfig(MatchState state)
{
    const cvar_t *cfg = nullptr;
    switch (state) {
    case MatchState::Warmup: cfg = &cvars_.cfgWarmup; break;
    case MatchState::FirstHalf:
    case MatchState::SecondHalf: cfg = &cvars_.cfgLive; break;
    case MatchState::HalfTime: cfg = &cvars_.cfgHalftime; break;
    case MatchState::Overtime: cfg = &cvars_.cfgOvertime; break;
    case MatchState::Finished: cfg = &cvars_.cfgEnd; break;
    default: break;
    }
    if (cfg && cfg->string && *cfg->string) {
        ExecuteConfigFile(cfg->string);
    }
}

bool Plugin::ExecuteConfigFile(const std::string &path)
{
    if (!IsSafeConfigPath(path)) {
        Log("Rejected unsafe config path: %s", path.c_str());
        return false;
    }

    ServerCommand("exec %s\n", path.c_str());
    return true;
}

void Plugin::ApplyStateRules(MatchState state)
{
    if (IsPracticeState(state)) {
        ApplyPracticeStateRules();
        return;
    }

    CancelTask("practice_enforce");
    if (state == MatchState::StartingLO3 || IsLiveState(state)) {
        ApplyLiveStateRules();
    }
}

void Plugin::ApplyPracticeStateRules()
{
    ServerCommand("mp_forcerespawn 1\n");
    ServerCommand("mp_roundtime 60\n");
    ServerCommand("mp_startmoney 16000\n");
    ServerCommand("mp_maxmoney 16000\n");
    ServerCommand("mp_give_player_c4 0\n");
    EnforcePracticeStatePlayers();
    Schedule("practice_enforce", 1.0f, true, [this]() { EnforcePracticeStatePlayers(); });
}

void Plugin::ApplyLiveStateRules()
{
    ServerCommand("mp_forcerespawn 0\n");
    ServerCommand("mp_roundtime 1.75\n");
    ServerCommand("mp_startmoney 800\n");
    ServerCommand("mp_maxmoney 16000\n");
    ServerCommand("mp_give_player_c4 1\n");
    ServerCommand("mp_c4timer 35\n");
    ServerCommand("mp_freezetime 15\n");
}

bool Plugin::IsSafeConfigPath(const std::string &path) const
{
    constexpr const char *base = "addons/xashmetapug/cfg/";
    if (path.empty() || path.size() > 120) {
        return false;
    }
    if (path.rfind(base, 0) != 0) {
        return false;
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    for (const char ch : path) {
        const bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '/' || ch == '_' || ch == '-' || ch == '.';
        if (!allowed) {
            return false;
        }
    }
    return path.size() > 4 && path.substr(path.size() - 4) == ".cfg";
}

void Plugin::StartReady()
{
    if (state_ == MatchState::Finished) {
        ResetMatch(true);
    }
    for (auto &player : players_) {
        player.ready = false;
    }
    SetState(MatchState::WaitingReady);
    const int required = GetRequiredReadyCount();
    Broadcast("[XMP] Type .ready to start. Need %d player(s) of %d connected.\n", required, ConnectedPlayers());
    if (CvarInt(cvars_.readyType) == 2) {
        Schedule("ready_timer", CvarFloat(cvars_.readyTime), false, [this]() { StartMatch(true); });
    }
}

void Plugin::CheckReady()
{
    if (state_ != MatchState::WaitingReady && state_ != MatchState::HalfTime) {
        return;
    }
    if (ReadyPlayers() >= GetRequiredReadyCount()) {
        Broadcast("[XMP] All required players are ready.\n");
        StartMatch(true);
    }
}

void Plugin::StartMatch(bool force)
{
    if (state_ == MatchState::Finished) {
        ResetMatch(true);
    }
    if (!force && ConnectedPlayers() < CvarInt(cvars_.playersMin)) {
        Broadcast("[XMP] Need %d player(s) to start.\n", CvarInt(cvars_.playersMin));
        return;
    }

    if (state_ == MatchState::HalfTime || (state_ == MatchState::WaitingReady && pendingLiveState_ == MatchState::SecondHalf)) {
        pendingLiveState_ = MatchState::SecondHalf;
    } else if (state_ == MatchState::Finished || state_ == MatchState::Disabled || state_ == MatchState::Warmup || state_ == MatchState::WaitingReady) {
        pendingLiveState_ = MatchState::FirstHalf;
    }
    if (pendingLiveState_ == MatchState::FirstHalf && !knifeRoundCompleted_) {
        StartKnifeRound();
        return;
    }
    StartLO3(pendingLiveState_);
}

void Plugin::StartKnifeRound()
{
    pendingLiveState_ = MatchState::FirstHalf;
    sideSelectionPending_ = false;
    knifeWinner_ = Team::Unknown;
    restarting_ = true;
    SetState(MatchState::KnifeRound);
    EnforceKnifeRoundWeapons();
    Broadcast("[XMP] Knife round starting. Winner chooses side with .stay or .swap.\n");
    ServerCommand("sv_restart 1\n");
    Schedule("knife_strip", 1.5f, false, [this]() { StripKnifeRoundWeapons(); });
}

void Plugin::StartLO3(MatchState liveState)
{
    pendingLiveState_ = liveState;
    lo3Step_ = 0;
    const bool preservePlayerScores = ShouldPreservePlayerScores(liveState);
    SetState(MatchState::StartingLO3);
    ApplyLiveStateRules();
    if (CvarInt(cvars_.lo3Enabled) <= 0) {
        FinishLO3();
        return;
    }
    Schedule("lo3", 1.0f, true, [this, preservePlayerScores]() {
        ++lo3Step_;
        if (lo3Step_ <= 2) {
            Broadcast("[XMP] Live on three restart %d/2.\n", lo3Step_);
            if (!preservePlayerScores) {
                ServerCommand("sv_restart 1\n");
            }
        }
        if (lo3Step_ >= 2) {
            CancelTask("lo3");
            Schedule("lo3_finish", 2.0f, false, [this]() { FinishLO3(); });
        }
    });
}

void Plugin::FinishLO3()
{
    this->restarting_ = false;
    const bool preservePlayerScores = ShouldPreservePlayerScores(pendingLiveState_);
    if (!preservePlayerScores) {
        lastObservedTScore_ = 0;
        lastObservedCTScore_ = 0;
    }
    RestoreKnifeRoundWeapons();
    SetState(pendingLiveState_);
    if (preservePlayerScores) {
        ResetLivePlayerLoadout(800);
    } else {
        ServerCommand("sv_restart 3\n");
    }
    ServerCommand("say \"[XMP] LIVE LIVE LIVE!\"\n");
    SyncDisplayedTeamScoresFromMatchScores(true);
    ReplayAllScoreInfo();
    Log("[XMP] LIVE LIVE LIVE!");
}

void Plugin::StopMatch()
{
    ResetMatch(true);
    SetState(MatchState::Warmup);
}

void Plugin::RestartMatch()
{
    ResetMatch(true);
    StartReady();
}

void Plugin::PauseMatch()
{
    if (paused_) {
        Broadcast("[XMP] Match is already paused.\n");
        return;
    }
    paused_ = true;
    Broadcast("[XMP] Match paused for %.0f seconds.\n", CvarFloat(cvars_.pauseTime));
    ServerCommand("pausable 1\n");
    Schedule("pause", CvarFloat(cvars_.pauseTime), false, [this]() { UnpauseMatch(); });
}

void Plugin::UnpauseMatch()
{
    if (!paused_) {
        return;
    }
    paused_ = false;
    CancelTask("pause");
    ServerCommand("pausable 0\n");
    Broadcast("[XMP] Match unpaused.\n");
}

void Plugin::SwapTeams()
{
    this->restarting_ = true;
    int movedT = 0;
    int movedCT = 0;
    int skipped = 0;
    for (int i = 1; i <= kMaxClients; ++i) {
        if (!players_[i].connected || !IsConnectedPlayerIndex(i)) continue;

        edict_t *entity = INDEXENT(i);
        if (FNullEnt(entity)) continue;

        if (players_[i].team != Team::Terrorist && players_[i].team != Team::CounterTerrorist) {
            ++skipped;
            continue;
        }

        const int targetTeam = (players_[i].team == Team::Terrorist) ? 2 : 1;
        char joinCommand[] = "jointeam %d\n";
        g_engfuncs.pfnClientCommand(entity, joinCommand, targetTeam);
        if (targetTeam == 2) ++movedT;
        else ++movedCT;
    }
    Log("SwapTeams: %d -> CT, %d -> T, %d skipped (spec/unknown)", movedT, movedCT, skipped);

    Broadcast("[XMP] Players have been switched. Team scores are tracked by the engine.\n");
}

void Plugin::SwapSideScores()
{
    std::swap(terroristScore_, ctScore_);
    std::swap(lastObservedTScore_, lastObservedCTScore_);
    SyncDisplayedTeamScoresFromMatchScores(false);
}

bool Plugin::ShouldPreservePlayerScores(MatchState liveState) const
{
    return liveState == MatchState::SecondHalf;
}

void Plugin::HandleRoundScore(Team team, int score)
{
    if (state_ == MatchState::KnifeRound) {
        HandleKnifeRoundScore(team, score);
        return;
    }

    if (this->syncingScoreboard_ || this->restarting_) {
        if (team == Team::Terrorist) lastObservedTScore_ = score;
        if (team == Team::CounterTerrorist) lastObservedCTScore_ = score;
        return;
    }

    if (state_ != MatchState::FirstHalf && state_ != MatchState::SecondHalf && state_ != MatchState::Overtime) {
        if (team == Team::Terrorist) lastObservedTScore_ = score;
        if (team == Team::CounterTerrorist) lastObservedCTScore_ = score;
        return;
    }

    bool increment = false;
    if (team == Team::Terrorist && score > lastObservedTScore_) {
        terroristScore_ += score - lastObservedTScore_;
        increment = true;
    } else if (team == Team::CounterTerrorist && score > lastObservedCTScore_) {
        ctScore_ += score - lastObservedCTScore_;
        increment = true;
    }

    if (team == Team::Terrorist) lastObservedTScore_ = score;
    if (team == Team::CounterTerrorist) lastObservedCTScore_ = score;

    if (increment) {
        this->restarting_ = false;
        ++halfRoundCount_;
        ++totalRoundCount_;
        if (state_ == MatchState::Overtime) {
            ++overtimeRoundCount_;
        }
        SetDisplayedTeamScore(team, team == Team::Terrorist ? terroristScore_ : ctScore_, true);
        Broadcast("[XMP] Score T %d - CT %d. Round %d/%d.\n", terroristScore_, ctScore_, totalRoundCount_, CvarInt(cvars_.matchRounds));
        EvaluateMatchProgress();
    }
}

void Plugin::HandleKnifeRoundScore(Team team, int score)
{
    if (team != Team::Terrorist && team != Team::CounterTerrorist) {
        return;
    }

    bool increment = false;
    if (team == Team::Terrorist && score > lastObservedTScore_) {
        increment = true;
    } else if (team == Team::CounterTerrorist && score > lastObservedCTScore_) {
        increment = true;
    }

    if (team == Team::Terrorist) lastObservedTScore_ = score;
    if (team == Team::CounterTerrorist) lastObservedCTScore_ = score;

    if (!increment || sideSelectionPending_) {
        return;
    }

    const Team winningTeam = team;
    knifeWinner_ = winningTeam;
    knifeRoundCompleted_ = true;
    sideSelectionPending_ = true;
    restarting_ = false;
    SetState(MatchState::SideSelection);
    Broadcast("[XMP] Knife round winner: %s. Winning players type .stay or .swap.\n", TeamName(knifeWinner_));
}

void Plugin::HandleSideSelection(edict_t *entity, bool swapSides)
{
    UpdatePlayer(entity);
    const int index = PlayerIndex(entity);
    if (state_ != MatchState::SideSelection || !sideSelectionPending_) {
        Say(entity, "[XMP] No side selection is pending.\n");
        return;
    }
    if (!IsConnectedPlayerIndex(index) || players_[index].team != knifeWinner_) {
        Say(entity, "[XMP] Only a player from the knife-winning side can choose.\n");
        return;
    }

    sideSelectionPending_ = false;
    if (swapSides) {
        Broadcast("[XMP] Knife winner chose to swap sides.\n");
        SwapTeams();
    } else {
        Broadcast("[XMP] Knife winner chose to stay.\n");
    }
    StartLO3(MatchState::FirstHalf);
}

bool Plugin::ShouldRewriteTeamScoreMessage() const
{
    return state_ == MatchState::HalfTime || state_ == MatchState::StartingLO3 || IsLiveState(state_);
}

void Plugin::CacheScoreInfo()
{
    const int index = message_.numbers[0];
    if (!IsConnectedPlayerIndex(index)) {
        return;
    }
    players_[index].scoreInfoValues = message_.numbers;
}

void Plugin::SendTeamScore(Team team)
{
    const int messageId = TeamScoreMessageId();
    if (messageId <= 0 || (team != Team::Terrorist && team != Team::CounterTerrorist)) {
        return;
    }

    replayingScoreMessages_ = true;
    g_engfuncs.pfnMessageBegin(MSG_ALL, messageId, nullptr, nullptr);
    g_engfuncs.pfnWriteString(EngineTeamScoreName(team));
    g_engfuncs.pfnWriteShort(DisplayedTeamScore(team));
    g_engfuncs.pfnMessageEnd();
    replayingScoreMessages_ = false;
}

void Plugin::SendTeamScoreMessages()
{
    SendTeamScore(Team::CounterTerrorist);
    SendTeamScore(Team::Terrorist);
}

void Plugin::SetDisplayedTeamScore(Team team, int score, bool resend)
{
    if (team == Team::Terrorist) {
        displayedTerroristScore_ = score;
    } else if (team == Team::CounterTerrorist) {
        displayedCTScore_ = score;
    } else {
        return;
    }

    if (resend) {
        SendTeamScore(team);
    }
}

void Plugin::SetDisplayedTeamScores(int terroristScore, int ctScore, bool resend)
{
    displayedTerroristScore_ = terroristScore;
    displayedCTScore_ = ctScore;
    if (resend) {
        SendTeamScoreMessages();
    }
}

void Plugin::SyncDisplayedTeamScoresFromMatchScores(bool resend)
{
    SetDisplayedTeamScores(terroristScore_, ctScore_, resend);
}

int Plugin::DisplayedTeamScore(Team team) const
{
    if (team == Team::Terrorist) return displayedTerroristScore_;
    if (team == Team::CounterTerrorist) return displayedCTScore_;
    return 0;
}

void Plugin::SendScoreInfo(int index)
{
    if (!IsConnectedPlayerIndex(index) || players_[index].scoreInfoValues.empty()) {
        return;
    }
    const int messageId = ScoreInfoMessageId();
    if (messageId <= 0) {
        return;
    }

    std::vector<int> values = players_[index].scoreInfoValues;
    if (values.size() >= 5) {
        values[4] = TeamNumber(players_[index].team);
    }

    replayingScoreMessages_ = true;
    g_engfuncs.pfnMessageBegin(MSG_ALL, messageId, nullptr, nullptr);
    g_engfuncs.pfnWriteByte(values[0]);
    for (size_t i = 1; i < values.size(); ++i) {
        g_engfuncs.pfnWriteShort(values[i]);
    }
    g_engfuncs.pfnMessageEnd();
    replayingScoreMessages_ = false;
}

void Plugin::ReplayAllScoreInfo()
{
    for (int i = 1; i <= kMaxClients; ++i) {
        SendScoreInfo(i);
    }
}

int Plugin::TeamScoreMessageId()
{
    if (teamScoreMessageId_ <= 0 && gpMetaUtilFuncs) {
        teamScoreMessageId_ = GET_USER_MSG_ID(PLID, "TeamScore", nullptr);
    }
    return teamScoreMessageId_;
}

int Plugin::ScoreInfoMessageId()
{
    if (scoreInfoMessageId_ <= 0 && gpMetaUtilFuncs) {
        scoreInfoMessageId_ = GET_USER_MSG_ID(PLID, "ScoreInfo", nullptr);
    }
    return scoreInfoMessageId_;
}

int Plugin::MoneyMessageId()
{
    if (moneyMessageId_ <= 0 && gpMetaUtilFuncs) {
        moneyMessageId_ = GET_USER_MSG_ID(PLID, "Money", nullptr);
    }
    return moneyMessageId_;
}

const char *Plugin::EngineTeamScoreName(Team team) const
{
    return team == Team::CounterTerrorist ? "CT" : "TERRORIST";
}

int Plugin::TeamNumber(Team team) const
{
    if (team == Team::Terrorist) return 1;
    if (team == Team::CounterTerrorist) return 2;
    if (team == Team::Spectator) return 3;
    return 0;
}

bool Plugin::IsKnifeRoundState(MatchState state) const
{
    return state == MatchState::KnifeRound || state == MatchState::SideSelection;
}

void Plugin::EnforceKnifeRoundWeapons()
{
    knifeRoundWeaponsEnforced_ = true;
    ServerCommand("mp_startmoney 0\n");
    ServerCommand("mp_maxmoney 0\n");
    for (int i = 1; i <= kMaxClients; ++i) {
        if (!players_[i].connected || FNullEnt(INDEXENT(i))) continue;
        EnforceKnifeRoundPlayerNative(INDEXENT(i));
    }
    Broadcast("[XMP] Knife-only mode: money locked and pistols stripped.\n");
    Log("EnforceKnifeRoundWeapons: money stripped and knife-only inventory applied for all players");
}

edict_t *Plugin::CreateNamedEntity(const char *classname) const
{
    if (!classname || !*classname) {
        return nullptr;
    }
    const string_t className = g_engfuncs.pfnAllocString(classname);
    return g_engfuncs.pfnCreateNamedEntity(className);
}

bool Plugin::StripPlayerWeaponsNative(edict_t *entity) const
{
    if (!entity || FNullEnt(entity) || !gpGamedllFuncs || !gpGamedllFuncs->dllapi_table) {
        return false;
    }

    edict_t *stripper = CreateNamedEntity("player_weaponstrip");
    if (!stripper || FNullEnt(stripper)) {
        return false;
    }

    MDLL_Spawn(stripper);
    MDLL_Use(stripper, entity);
    g_engfuncs.pfnRemoveEntity(stripper);
    return true;
}

bool Plugin::GiveItemNative(edict_t *entity, const char *classname) const
{
    if (!entity || FNullEnt(entity) || !classname || !*classname || !gpGamedllFuncs || !gpGamedllFuncs->dllapi_table) {
        return false;
    }
    if (std::strncmp(classname, "weapon_", 7) != 0 && std::strncmp(classname, "ammo_", 5) != 0 && std::strncmp(classname, "item_", 5) != 0) {
        return false;
    }

    edict_t *item = CreateNamedEntity(classname);
    if (!item || FNullEnt(item)) {
        return false;
    }

    item->v.origin = entity->v.origin;
    item->v.spawnflags |= SF_NORESPAWN;
    MDLL_Spawn(item);
    MDLL_Touch(item, entity);

    if (!FNullEnt(item) && item->v.owner != entity && item->v.solid != SOLID_NOT) {
        g_engfuncs.pfnRemoveEntity(item);
        return false;
    }
    return true;
}

bool Plugin::SetPlayerMoneyNative(edict_t *entity, int money, bool flash)
{
    if (!entity || FNullEnt(entity)) {
        return false;
    }

    CBasePlayer *player = CBasePlayer::Instance(entity);
    if (!player) {
        return false;
    }

    player->m_iAccount = money;
    const int messageId = MoneyMessageId();
    if (messageId > 0) {
        g_engfuncs.pfnMessageBegin(MSG_ONE, messageId, nullptr, entity);
        g_engfuncs.pfnWriteLong(money);
        g_engfuncs.pfnWriteByte(flash ? 1 : 0);
        g_engfuncs.pfnMessageEnd();
    }
    return true;
}

bool Plugin::RemovePlayerC4Native(edict_t *entity) const
{
    if (!entity || FNullEnt(entity)) {
        return false;
    }

    CBasePlayer *player = CBasePlayer::Instance(entity);
    if (!player) {
        return false;
    }

    bool removed = false;
    player->m_bHasC4 = false;
    entity->v.weapons &= ~(1 << WEAPON_C4);
    player->ForEachItem([&](CBasePlayerItem *item) {
        if (!item || item->m_iId != WEAPON_C4) {
            return false;
        }
        player->RemovePlayerItem(item);
        item->Kill();
        removed = true;
        return true;
    });
    return removed;
}

void Plugin::EnforcePracticePlayer(edict_t *entity)
{
    if (!entity || FNullEnt(entity)) {
        return;
    }

    const int index = PlayerIndex(entity);
    if (!IsConnectedPlayerIndex(index)) {
        return;
    }
    if (players_[index].team != Team::Terrorist && players_[index].team != Team::CounterTerrorist) {
        return;
    }

    SetPlayerMoneyNative(entity, 16000, false);
    RemovePlayerC4Native(entity);
}

void Plugin::EnforcePracticeStatePlayers()
{
    if (!IsPracticeState(state_)) {
        CancelTask("practice_enforce");
        return;
    }

    for (int i = 1; i <= kMaxClients; ++i) {
        if (!players_[i].connected || FNullEnt(INDEXENT(i))) continue;
        EnforcePracticePlayer(INDEXENT(i));
    }
}

void Plugin::ResetLivePlayerLoadout(int money)
{
    ServerCommand("mp_startmoney %d\n", money);
    for (int i = 1; i <= kMaxClients; ++i) {
        if (!players_[i].connected || FNullEnt(INDEXENT(i))) continue;
        if (players_[i].team != Team::Terrorist && players_[i].team != Team::CounterTerrorist) {
            continue;
        }
        edict_t *entity = INDEXENT(i);
        ResetLivePlayerInventory(entity);
        SetPlayerMoneyNative(entity, money, false);
    }
    Log("ResetLivePlayerLoadout: applied default inventory and %d money to active players", money);
}

void Plugin::ResetLivePlayerInventory(edict_t *entity)
{
    if (!entity || FNullEnt(entity)) {
        return;
    }

    const int index = PlayerIndex(entity);
    if (!IsConnectedPlayerIndex(index)) {
        return;
    }

    CBasePlayer *player = CBasePlayer::Instance(entity);
    if (player) {
        player->m_iKevlar = ARMOR_NONE;
        player->m_bHasPrimary = false;
        player->m_bHasC4 = false;
        player->m_bHasDefuser = false;
        player->m_bHasNightVision = false;
        std::fill(std::begin(player->m_rgAmmo), std::end(player->m_rgAmmo), 0);
        std::fill(std::begin(player->m_rgAmmoLast), std::end(player->m_rgAmmoLast), 0);
    }
    entity->v.armorvalue = 0.0f;

    StripPlayerWeaponsNative(entity);
    GiveItemNative(entity, "weapon_knife");
    if (players_[index].team == Team::Terrorist) {
        GiveItemNative(entity, "weapon_glock18");
    } else if (players_[index].team == Team::CounterTerrorist) {
        GiveItemNative(entity, "weapon_usp");
    }
}

void Plugin::EnforceKnifeRoundPlayerNative(edict_t *entity)
{
    if (!entity || FNullEnt(entity)) {
        return;
    }

    CBasePlayer *player = CBasePlayer::Instance(entity);
    if (player) {
        player->m_iKevlar = ARMOR_NONE;
        player->m_bHasPrimary = false;
        player->m_bHasC4 = false;
        player->m_bHasDefuser = false;
        player->m_bHasNightVision = false;
        std::fill(std::begin(player->m_rgAmmo), std::end(player->m_rgAmmo), 0);
        std::fill(std::begin(player->m_rgAmmoLast), std::end(player->m_rgAmmoLast), 0);
    }
    entity->v.armorvalue = 0.0f;

    SetPlayerMoneyNative(entity, 0, false);
    StripPlayerWeaponsNative(entity);
    GiveItemNative(entity, "weapon_knife");
}

void Plugin::StripKnifeRoundWeapons()
{
    if (!knifeRoundWeaponsEnforced_ || !IsKnifeRoundState(state_)) {
        CancelTask("knife_strip");
        return;
    }
    for (int i = 1; i <= kMaxClients; ++i) {
        if (!players_[i].connected || FNullEnt(INDEXENT(i))) continue;
        if (players_[i].team != Team::Terrorist && players_[i].team != Team::CounterTerrorist) {
            continue;
        }
        edict_t *entity = INDEXENT(i);
        EnforceKnifeRoundPlayerNative(entity);
    }
}

void Plugin::RestoreKnifeRoundWeapons()
{
    if (!knifeRoundWeaponsEnforced_) {
        return;
    }
    knifeRoundWeaponsEnforced_ = false;
    CancelTask("knife_strip");
    ServerCommand("mp_startmoney 800\n");
    ServerCommand("mp_maxmoney 16000\n");
    Log("RestoreKnifeRoundWeapons: money defaults restored, knife_strip task cancelled");
}

void Plugin::EvaluateMatchProgress()
{
    Log("EvaluateMatchProgress: state=%s, totalRounds=%d, halfRounds=%d, terrorist=%d, ct=%d", StateName(state_), totalRoundCount_, halfRoundCount_, terroristScore_, ctScore_);

    const int firstTo = CvarInt(cvars_.firstTo);
    const int overtimeFirstTo = CvarInt(cvars_.overtimeFirstTo);

    if (state_ == MatchState::FirstHalf) {
        if (firstTo > 0 && (terroristScore_ >= firstTo || ctScore_ >= firstTo)) {
            FinishMatch();
            return;
        }
        if (halfRoundCount_ >= CvarInt(cvars_.halfRounds)) {
            EnterHalftime();
            return;
        }
    }

    if (state_ == MatchState::SecondHalf) {
        if (firstTo > 0 && (terroristScore_ >= firstTo || ctScore_ >= firstTo)) {
            FinishMatch();
            return;
        }
        if (totalRoundCount_ >= CvarInt(cvars_.matchRounds)) {
            if (terroristScore_ == ctScore_ && CvarInt(cvars_.overtimeEnabled) > 0) {
                EnterOvertime();
            } else {
                FinishMatch();
            }
            return;
        }
    }

    if (state_ == MatchState::Overtime) {
        const int overtimeTerroristScore = terroristScore_ - overtimeTerroristStartScore_;
        const int overtimeCTScore = ctScore_ - overtimeCTStartScore_;
        if (overtimeFirstTo > 0 && (overtimeTerroristScore >= overtimeFirstTo || overtimeCTScore >= overtimeFirstTo)) {
            FinishMatch();
            return;
        }
        if (overtimeRoundCount_ >= CvarInt(cvars_.overtimeRounds)) {
            if (terroristScore_ == ctScore_) {
                EnterOvertime();
            } else {
                FinishMatch();
            }
        }
    }
}

void Plugin::EnterHalftime()
{
    halfRoundCount_ = 0;
    pendingLiveState_ = MatchState::SecondHalf;
    SetState(MatchState::HalfTime);
    Broadcast("[XMP] Halftime. Score T %d - CT %d. Swap sides and type .ready.\n", terroristScore_, ctScore_);
    SwapSideScores();
    SyncDisplayedTeamScoresFromMatchScores(true);
    SwapTeams();
    StartReady();
}

void Plugin::EnterOvertime()
{
    overtimeRoundCount_ = 0;
    overtimeTerroristStartScore_ = terroristScore_;
    overtimeCTStartScore_ = ctScore_;
    pendingLiveState_ = MatchState::Overtime;
    SetState(MatchState::Overtime);
    Broadcast("[XMP] Overtime started.\n");
}

void Plugin::FinishMatch()
{
    SetState(MatchState::Finished);
    if (terroristScore_ == ctScore_) {
        Broadcast("[XMP] Match finished tied: %d-%d.\n", terroristScore_, ctScore_);
    } else {
        Broadcast("[XMP] Match finished. Winner: %s (%d-%d).\n", terroristScore_ > ctScore_ ? "Terrorists" : "Counter-Terrorists", terroristScore_, ctScore_);
    }
}

bool Plugin::DispatchCommand(edict_t *entity, const std::string &raw)
{
    if (raw.empty()) {
        return false;
    }
    if (CvarInt(cvars_.enabled) <= 0) {
        return false;
    }
    const std::string adminPrefix = CvarString(cvars_.adminPrefix);
    const std::string playerPrefix = CvarString(cvars_.playerPrefix);
    if (!adminPrefix.empty() && raw.rfind(adminPrefix, 0) == 0) {
        return DispatchAdminCommand(entity, raw.substr(adminPrefix.size()));
    }
    if (!playerPrefix.empty() && raw.rfind(playerPrefix, 0) == 0) {
        return DispatchPlayerCommand(entity, raw.substr(playerPrefix.size()));
    }
    return false;
}

bool Plugin::DispatchPlayerCommand(edict_t *entity, const std::string &command)
{
    const std::string normalized = TrimCommand(command);
    if (normalized == "ready") {
        SetReady(entity, true);
        CheckReady();
    } else if (normalized == "notready") {
        SetReady(entity, false);
    } else if (normalized == "stay") {
        HandleSideSelection(entity, false);
    } else if (normalized == "swap") {
        HandleSideSelection(entity, true);
    } else if (normalized == "status") {
        Say(entity, "[XMP] State: %s. Ready %d/%d. Players %d/%d.\n", StateName(state_), ReadyPlayers(), GetRequiredReadyCount(), ConnectedPlayers(), CvarInt(cvars_.playersMax));
    } else if (normalized == "score") {
        Say(entity, "[XMP] Score T %d - CT %d. Round %d/%d.\n", terroristScore_, ctScore_, totalRoundCount_, CvarInt(cvars_.matchRounds));
    } else if (normalized == "help") {
        Say(entity, "[XMP] Commands: .ready .notready .status .score .help\n");
    } else {
        return false;
    }
    return true;
}

bool Plugin::DispatchAdminCommand(edict_t *entity, const std::string &command)
{
    const std::string normalized = TrimCommand(command);
    if (!IsAdmin(entity)) {
        Say(entity, "[XMP] You do not have access to that command.\n");
        return true;
    }

    if (normalized == "start") StartReady();
    else if (normalized == "forcestart") StartMatch(true);
    else if (normalized == "stop") StopMatch();
    else if (normalized == "restart") RestartMatch();
    else if (normalized == "pause") PauseMatch();
    else if (normalized == "unpause") UnpauseMatch();
    else if (normalized == "swap") {
        if (IsLiveState(state_)) {
            Say(entity, "[XMP] Side switching is disabled while LIVE.\n");
        } else {
            SwapTeams();
        }
    }
    else if (normalized == "score") Broadcast("[XMP] Score T %d - CT %d. Round %d/%d.\n", terroristScore_, ctScore_, totalRoundCount_, CvarInt(cvars_.matchRounds));
    else if (normalized == "reload") { LoadAdmins(); Broadcast("[XMP] Admin list reloaded.\n"); }
    else return false;
    return true;
}

bool Plugin::IsAdmin(edict_t *entity) const
{
    const int index = PlayerIndex(entity);
    return IsConnectedPlayerIndex(index) && players_[index].admin;
}

bool Plugin::IsLiveState(MatchState state) const
{
    return state == MatchState::FirstHalf || state == MatchState::SecondHalf || state == MatchState::Overtime;
}

bool Plugin::IsPracticeState(MatchState state) const
{
    return state == MatchState::Warmup || state == MatchState::HalfTime;
}

bool Plugin::IsSideSwitchBlocked(MatchState state) const
{
    return IsLiveState(state) || state == MatchState::KnifeRound || state == MatchState::SideSelection;
}

bool Plugin::IsConnectedPlayerIndex(int index) const
{
    return index > 0 && index <= kMaxClients && players_[index].connected;
}

int Plugin::PlayerIndex(edict_t *entity) const
{
    if (!entity || FNullEnt(entity)) {
        return 0;
    }
    return ENTINDEX(entity);
}

int Plugin::ConnectedPlayers() const
{
    int count = 0;
    for (int i = 1; i <= kMaxClients; ++i) {
        if (players_[i].connected && !FNullEnt(INDEXENT(i))) ++count;
    }
    return count;
}

int Plugin::ReadyPlayers() const
{
    int count = 0;
    for (int i = 1; i <= kMaxClients; ++i) {
        if (players_[i].connected && players_[i].ready && !FNullEnt(INDEXENT(i))) ++count;
    }
    return count;
}

int Plugin::GetRequiredReadyCount() const
{
    const int required = std::min(CvarInt(cvars_.playersMin), ConnectedPlayers());
    return required > 0 ? required : 1;
}

void Plugin::SetReady(edict_t *entity, bool ready)
{
    UpdatePlayer(entity);
    const int index = PlayerIndex(entity);
    if (!IsConnectedPlayerIndex(index)) {
        return;
    }
    players_[index].ready = ready;
    Broadcast("[XMP] %s is %sready (%d/%d).\n", players_[index].name.c_str(), ready ? "" : "not ", ReadyPlayers(), GetRequiredReadyCount());
}

void Plugin::UpdatePlayer(edict_t *entity)
{
    const int index = PlayerIndex(entity);
    if (index <= 0 || index > kMaxClients) {
        return;
    }
    PlayerInfo &player = players_[index];
    player.connected = true;
    if (player.name.empty()) {
        player.name = Format("player%d", index);
    }
    player.admin = !player.authId.empty() && admins_.count(player.authId) > 0;
}

void Plugin::Schedule(const std::string &name, float delay, bool repeat, std::function<void()> callback)
{
    CancelTask(name);
    tasks_.push_back({name, (gpGlobals ? gpGlobals->time : 0.0f) + delay, delay, repeat, std::move(callback)});
}

void Plugin::CancelTask(const std::string &name)
{
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [&](const ScheduledTask &task) { return task.name == name; }), tasks_.end());
}

void Plugin::ClearTasks()
{
    tasks_.clear();
}

void Plugin::Say(edict_t *target, const char *format, ...) const
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (target) {
        g_engfuncs.pfnClientPrintf(target, print_chat, buffer);
    } else {
        for (int i = 1; i <= kMaxClients; ++i) {
            if (players_[i].connected) {
                g_engfuncs.pfnClientPrintf(INDEXENT(i), print_chat, buffer);
            }
        }
    }
}

void Plugin::Broadcast(const char *format, ...) const
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Say(nullptr, "%s", buffer);
    Log("%s", buffer);
}

void Plugin::ServerCommand(const char *format, ...) const
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(buffer)) {
        Log("Skipped truncated server command");
        return;
    }
    g_engfuncs.pfnServerCommand(buffer);
}

void Plugin::Log(const char *format, ...) const
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (gpMetaUtilFuncs) {
        gpMetaUtilFuncs->pfnLogConsole(PLID, "[XashMetaPUG] %s", buffer);
    } else {
        g_engfuncs.pfnAlertMessage(at_logged, "[XashMetaPUG] %s\n", buffer);
    }
}

std::string Plugin::Format(const char *format, ...) const
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

std::string Plugin::TrimCommand(const std::string &command) const
{
    const auto first = command.find_first_not_of(" \t\r\n\"");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = command.find_last_not_of(" \t\r\n\"");
    const std::string trimmed = command.substr(first, last - first + 1);
    const auto separator = trimmed.find_first_of(" \t");
    return separator == std::string::npos ? trimmed : trimmed.substr(0, separator);
}

std::string Plugin::CvarString(const cvar_t &cvar) const { return cvar.string ? cvar.string : ""; }
int Plugin::CvarInt(const cvar_t &cvar) const { return static_cast<int>(cvar.value); }
float Plugin::CvarFloat(const cvar_t &cvar) const { return cvar.value; }

const char *Plugin::StateName(MatchState state) const
{
    switch (state) {
    case MatchState::Disabled: return "Disabled";
    case MatchState::Warmup: return "Warmup";
    case MatchState::WaitingReady: return "WaitingReady";
    case MatchState::KnifeRound: return "KnifeRound";
    case MatchState::SideSelection: return "SideSelection";
    case MatchState::StartingLO3: return "StartingLO3";
    case MatchState::FirstHalf: return "FirstHalf";
    case MatchState::HalfTime: return "HalfTime";
    case MatchState::SecondHalf: return "SecondHalf";
    case MatchState::Overtime: return "Overtime";
    case MatchState::Finished: return "Finished";
    }
    return "Unknown";
}

Team Plugin::ParseTeamName(const std::string &name) const
{
    if (name == "TERRORIST" || name == "Terrorists" || name == "T") return Team::Terrorist;
    if (name == "CT" || name == "Counter-Terrorist" || name == "Counter-Terrorists") return Team::CounterTerrorist;
    if (name == "SPECTATOR" || name == "Spectator") return Team::Spectator;
    return Team::Unknown;
}

const char *Plugin::TeamName(Team team) const
{
    switch (team) {
    case Team::Terrorist: return "T";
    case Team::CounterTerrorist: return "CT";
    case Team::Spectator: return "SPEC";
    case Team::Unknown: break;
    }
    return "?";
}



} // namespace xmp
