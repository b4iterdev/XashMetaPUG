#pragma once

#ifndef _WIN32
#include <strings.h>
#endif

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <extdll.h>
#include <eiface.h>
#include <meta_api.h>

extern enginefuncs_t g_engfuncs;
extern globalvars_t *gpGlobals;
extern meta_globals_t *gpMetaGlobals;
extern gamedll_funcs_t *gpGamedllFuncs;
extern mutil_funcs_t *gpMetaUtilFuncs;

namespace xmp {

constexpr int kMaxClients = 32;

enum class MatchState {
    Disabled,
    Warmup,
    WaitingReady,
    StartingLO3,
    FirstHalf,
    HalfTime,
    SecondHalf,
    Overtime,
    Finished,
};

enum class Team {
    Unknown,
    Terrorist,
    CounterTerrorist,
    Spectator,
};

struct PlayerInfo {
    bool connected = false;
    bool ready = false;
    bool admin = false;
    int userId = 0;
    Team team = Team::Unknown;
    std::string authId;
    std::string name;
};

struct ScheduledTask {
    std::string name;
    float executeAt = 0.0f;
    float interval = 0.0f;
    bool repeat = false;
    std::function<void()> callback;
};

struct Cvars {
    cvar_t enabled{};
    cvar_t adminPrefix{};
    cvar_t playerPrefix{};
    cvar_t playersMin{};
    cvar_t playersMax{};
    cvar_t readyType{};
    cvar_t readyTime{};
    cvar_t matchRounds{};
    cvar_t halfRounds{};
    cvar_t overtimeEnabled{};
    cvar_t overtimeRounds{};
    cvar_t lo3Enabled{};
    cvar_t pauseTime{};
    cvar_t votePercent{};
    cvar_t cfgWarmup{};
    cvar_t cfgLive{};
    cvar_t cfgHalftime{};
    cvar_t cfgOvertime{};
    cvar_t cfgEnd{};
    cvar_t debugMessages{};
};

class Plugin {
public:
    void OnMetaAttach();
    void OnServerActivate();
    void OnServerDeactivate();
    void OnStartFrame();
    bool OnClientConnect(edict_t *entity, const char *name);
    void OnClientPutInServer(edict_t *entity);
    void OnClientDisconnect(edict_t *entity);
    bool OnClientCommand(edict_t *entity);

    void OnMessageBegin(int destination, int type, const float *origin, edict_t *entity);
    void OnWriteByte(int value);
    void OnWriteChar(int value);
    void OnWriteShort(int value);
    void OnWriteLong(int value);
    void OnWriteString(const char *value);
    void OnMessageEnd();
    void ForceStartFromServer();

private:
    void RegisterCvars();
    void RegisterCvar(cvar_t &cvar, const char *name, const char *value, int flags = FCVAR_EXTDLL);
    void LoadAdmins();
    void ResetMatch(bool keepWarmup);
    void SetState(MatchState next);
    void ExecuteStateConfig(MatchState state);
    bool ExecuteConfigFile(const std::string &path);
    bool IsSafeConfigPath(const std::string &path) const;
    void StartReady();
    void CheckReady();
    void StartMatch(bool force);
    void StartLO3(MatchState liveState);
    void FinishLO3();
    void StopMatch();
    void RestartMatch();
    void PauseMatch();
    void UnpauseMatch();
    void SwapTeams();
    void UpdateScoreboard();
    void HandleRoundScore(Team team, int score);
    void EvaluateMatchProgress();
    void EnterHalftime();
    void EnterOvertime();
    void FinishMatch();

    bool DispatchCommand(edict_t *entity, const std::string &raw);
    bool DispatchPlayerCommand(edict_t *entity, const std::string &command);
    bool DispatchAdminCommand(edict_t *entity, const std::string &command);
    bool IsAdmin(edict_t *entity) const;
    bool IsConnectedPlayerIndex(int index) const;
    int PlayerIndex(edict_t *entity) const;
    int ConnectedPlayers() const;
    int ReadyPlayers() const;
    void SetReady(edict_t *entity, bool ready);
    void UpdatePlayer(edict_t *entity);
    void Schedule(const std::string &name, float delay, bool repeat, std::function<void()> callback);
    void CancelTask(const std::string &name);
    void ClearTasks();

    void Say(edict_t *target, const char *format, ...) const;
    void Broadcast(const char *format, ...) const;
    void ServerCommand(const char *format, ...) const;
    void Log(const char *format, ...) const;
    std::string Format(const char *format, ...) const;
    std::string TrimCommand(const std::string &command) const;
    std::string CvarString(const cvar_t &cvar) const;
    int CvarInt(const cvar_t &cvar) const;
    float CvarFloat(const cvar_t &cvar) const;
    const char *StateName(MatchState state) const;
    Team ParseTeamName(const std::string &name) const;

    Cvars cvars_{};
    MatchState state_ = MatchState::Disabled;
    MatchState pendingLiveState_ = MatchState::FirstHalf;
    std::array<PlayerInfo, kMaxClients + 1> players_{};
    std::set<std::string> admins_{};
    std::vector<ScheduledTask> tasks_{};
    int lo3Step_ = 0;
    int halfRoundCount_ = 0;
    int totalRoundCount_ = 0;
    int overtimeRoundCount_ = 0;
    int terroristScore_ = 0;
    int ctScore_ = 0;
    int lastObservedTScore_ = 0;
    int lastObservedCTScore_ = 0;
    bool paused_ = false;
    bool restarting_ = false;

    struct MessageCapture {
        int type = 0;
        std::string name;
        std::vector<int> numbers;
        std::vector<std::string> strings;
    } message_{};
};

Plugin &GetPlugin();

} // namespace xmp
