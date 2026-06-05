#include "plugin.h"

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

void Plugin::OnMessageBegin(int destination, int type, const float *origin, edict_t *entity)
{
    message_ = MessageCapture{};
    message_.type = type;
    int size = 0;
    const char *name = gpMetaUtilFuncs ? GET_USER_MSG_NAME(PLID, type, &size) : nullptr;
    if (name) {
        message_.name = name;
    }
}

void Plugin::OnWriteByte(int value) { message_.numbers.push_back(value); }
void Plugin::OnWriteChar(int value) { message_.numbers.push_back(value); }
void Plugin::OnWriteShort(int value) { message_.numbers.push_back(value); }
void Plugin::OnWriteLong(int value) { message_.numbers.push_back(value); }
void Plugin::OnWriteString(const char *value) { 
    message_.strings.emplace_back(value ? value : ""); 
    if (message_.name == "TextMsg" && value && strstr(value, "#Game_Commencing")) {
        Log("Detected Game Commencing, resetting match state.");
        ResetMatch(true);
        SetState(MatchState::Warmup);
    }
}

void Plugin::OnMessageEnd()
{
    if (CvarInt(cvars_.debugMessages) > 0 && !message_.name.empty()) {
        Log("msg=%s strings=%zu nums=%zu", message_.name.c_str(), message_.strings.size(), message_.numbers.size());
    }

    if (message_.name == "TextMsg") {
        return;
    }

    if (message_.name == "TeamScore" && !message_.strings.empty() && !message_.numbers.empty()) {
        HandleRoundScore(ParseTeamName(message_.strings[0]), message_.numbers[0]);
    } else if (message_.name == "TeamInfo" && message_.strings.size() >= 2 && !message_.numbers.empty()) {
        const int index = message_.numbers[0];
        if (IsConnectedPlayerIndex(index)) {
            players_[index].team = ParseTeamName(message_.strings[1]);
        }
    }
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
    RegisterCvar(cvars_.overtimeEnabled, "xmp_overtime_enabled", "1");
    RegisterCvar(cvars_.overtimeRounds, "xmp_overtime_rounds", "6");
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
    for (auto &player : players_) {
        player.ready = false;
    }
    lo3Step_ = 0;
    halfRoundCount_ = 0;
    totalRoundCount_ = 0;
    overtimeRoundCount_ = 0;
    terroristScore_ = 0;
    ctScore_ = 0;
    lastObservedTScore_ = 0;
    lastObservedCTScore_ = 0;
    paused_ = false;
    state_ = keepWarmup ? MatchState::Warmup : MatchState::Disabled;
}

void Plugin::SetState(MatchState next)
{
    state_ = next;
    Broadcast("[XMP] State: %s\n", StateName(state_));
    ExecuteStateConfig(state_);
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
    for (auto &player : players_) {
        player.ready = false;
    }
    SetState(MatchState::WaitingReady);
    Broadcast("[XMP] Type .ready to start. Need %d player(s).\n", CvarInt(cvars_.playersMin));
    if (CvarInt(cvars_.readyType) == 2) {
        Schedule("ready_timer", CvarFloat(cvars_.readyTime), false, [this]() { StartMatch(true); });
    }
}

void Plugin::CheckReady()
{
    if (state_ != MatchState::WaitingReady && state_ != MatchState::HalfTime) {
        return;
    }
    if (ReadyPlayers() >= CvarInt(cvars_.playersMin)) {
        Broadcast("[XMP] All required players are ready.\n");
        StartMatch(true);
    }
}

void Plugin::StartMatch(bool force)
{
    if (!force && ConnectedPlayers() < CvarInt(cvars_.playersMin)) {
        Broadcast("[XMP] Need %d player(s) to start.\n", CvarInt(cvars_.playersMin));
        return;
    }

    if (state_ == MatchState::HalfTime || (state_ == MatchState::WaitingReady && pendingLiveState_ == MatchState::SecondHalf)) {
        pendingLiveState_ = MatchState::SecondHalf;
    } else if (state_ == MatchState::Finished || state_ == MatchState::Disabled || state_ == MatchState::Warmup || state_ == MatchState::WaitingReady) {
        pendingLiveState_ = MatchState::FirstHalf;
    }
    StartLO3(pendingLiveState_);
}

void Plugin::StartLO3(MatchState liveState)
{
    pendingLiveState_ = liveState;
    lo3Step_ = 0;
    SetState(MatchState::StartingLO3);
    if (CvarInt(cvars_.lo3Enabled) <= 0) {
        FinishLO3();
        return;
    }
    Schedule("lo3", 1.0f, true, [this]() {
        ++lo3Step_;
        if (lo3Step_ <= 2) {
            Broadcast("[XMP] Live on three restart %d/2.\n", lo3Step_);
            ServerCommand("sv_restart 1\n");
        }
        if (lo3Step_ >= 2) {
            CancelTask("lo3");
            Schedule("lo3_finish", 2.0f, false, [this]() { FinishLO3(); });
        }
    });
}

void Plugin::FinishLO3()
{
    lastObservedTScore_ = 0;
    lastObservedCTScore_ = 0;
    SetState(pendingLiveState_);
    ServerCommand("sv_restart 3\n");
    ServerCommand("say \"[XMP] LIVE LIVE LIVE!\"\n");
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

void xmp::Plugin::SwapTeams()
{
    this->restarting_ = true;
    for (int i = 1; i <= kMaxClients; ++i) {
        if (!players_[i].connected || !IsConnectedPlayerIndex(i)) continue;

        edict_t *entity = INDEXENT(i);
        if (FNullEnt(entity)) continue;

        Log("SwapTeams: Player %d (%s) current team: %d", i, players_[i].name.c_str(), (int)players_[i].team);

        const int targetTeam = (players_[i].team == Team::Terrorist) ? 2 : 1;
        g_engfuncs.pfnClientCommand(entity, "chooseteam\n");
        g_engfuncs.pfnClientCommand(entity, "jointeam %d\n", targetTeam);
        Log("SwapTeams: Player %d joined team %d", i, targetTeam);
    }

    std::swap(terroristScore_, ctScore_);
    Schedule("update_scoreboard", 0.1f, false, [this]() { UpdateScoreboard(); });
    Broadcast("[XMP] Tracked team scores swapped. Players should switch sides now.\n");
}


void Plugin::UpdateScoreboard()
{
    this->syncingScoreboard_ = true;
    int msgTeamScore = gpMetaUtilFuncs ? GET_USER_MSG_ID(PLID, "TeamScore", nullptr) : -1;
    if (msgTeamScore == -1) {
        msgTeamScore = g_engfuncs.pfnRegUserMsg("TeamScore", -1);
    }

    auto SendScore = [&](const char *teamName, int score) {
        g_engfuncs.pfnMessageBegin(MSG_ALL, msgTeamScore, nullptr, nullptr);
        g_engfuncs.pfnWriteString(teamName);
        g_engfuncs.pfnWriteShort(score);
        g_engfuncs.pfnMessageEnd();
    };

    SendScore("TERRORIST", terroristScore_);
    SendScore("CT", ctScore_);
    this->syncingScoreboard_ = false;
}

void Plugin::HandleRoundScore(Team team, int score)
{
    if (this->syncingScoreboard_ || this->restarting_) {
        lastObservedTScore_ = score;
        lastObservedCTScore_ = score;
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
        Broadcast("[XMP] Score T %d - CT %d. Round %d/%d.\n", terroristScore_, ctScore_, totalRoundCount_, CvarInt(cvars_.matchRounds));
        Schedule("update_scoreboard", 0.1f, false, [this]() { UpdateScoreboard(); });
        EvaluateMatchProgress();
    }
}

void Plugin::EvaluateMatchProgress()
{
    Log("EvaluateMatchProgress: state=%s, totalRounds=%d, halfRounds=%d, terrorist=%d, ct=%d", StateName(state_), totalRoundCount_, halfRoundCount_, terroristScore_, ctScore_);
    if (state_ == MatchState::FirstHalf && halfRoundCount_ >= CvarInt(cvars_.halfRounds)) {
        EnterHalftime();
        return;
    }

    if (state_ == MatchState::SecondHalf && totalRoundCount_ >= CvarInt(cvars_.matchRounds)) {
        if (terroristScore_ == ctScore_ && CvarInt(cvars_.overtimeEnabled) > 0) {
            EnterOvertime();
        } else {
            FinishMatch();
        }
        return;
    }

    if (state_ == MatchState::Overtime && overtimeRoundCount_ >= CvarInt(cvars_.overtimeRounds)) {
        if (terroristScore_ == ctScore_) {
            EnterOvertime();
        } else {
            FinishMatch();
        }
    }
}

void Plugin::EnterHalftime()
{
    halfRoundCount_ = 0;
    pendingLiveState_ = MatchState::SecondHalf;
    lastObservedTScore_ = 0;
    lastObservedCTScore_ = 0;
    SetState(MatchState::HalfTime);
    Broadcast("[XMP] Halftime. Score T %d - CT %d. Swap sides and type .ready.\n", terroristScore_, ctScore_);
    SwapTeams();
    StartReady();
}

void Plugin::EnterOvertime()
{
    overtimeRoundCount_ = 0;
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
    } else if (normalized == "status") {
        Say(entity, "[XMP] State: %s. Ready %d/%d. Players %d/%d.\n", StateName(state_), ReadyPlayers(), CvarInt(cvars_.playersMin), ConnectedPlayers(), CvarInt(cvars_.playersMax));
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
    else if (normalized == "swap") SwapTeams();
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

void Plugin::SetReady(edict_t *entity, bool ready)
{
    UpdatePlayer(entity);
    const int index = PlayerIndex(entity);
    if (!IsConnectedPlayerIndex(index)) {
        return;
    }
    players_[index].ready = ready;
    Broadcast("[XMP] %s is %sready (%d/%d).\n", players_[index].name.c_str(), ready ? "" : "not ", ReadyPlayers(), CvarInt(cvars_.playersMin));
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

} // namespace xmp
