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
    KnifeRound,
    SideSelection,
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
    std::vector<int> scoreInfoValues;
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
    cvar_t firstTo{};
    cvar_t overtimeEnabled{};
    cvar_t overtimeRounds{};
    cvar_t overtimeFirstTo{};
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

    bool OnMessageBegin(int destination, int type, const float *origin, edict_t *entity);
    bool OnWriteByte(int value);
    bool OnWriteChar(int value);
    bool OnWriteShort(int value);
    bool OnWriteLong(int value);
    bool OnWriteString(const char *value);
    bool OnMessageEnd();
    void ForceStartFromServer();

private:
    void RegisterCvars();
    void RegisterCvar(cvar_t &cvar, const char *name, const char *value, int flags = FCVAR_EXTDLL);
    void LoadAdmins();
    void ResetMatch(bool keepWarmup);
    void SetState(MatchState next);
    void ExecuteStateConfig(MatchState state);
    void ApplyStateRules(MatchState state);
    void ApplyPracticeStateRules();
    void ApplyLiveStateRules();
    bool ExecuteConfigFile(const std::string &path);
    bool IsSafeConfigPath(const std::string &path) const;
    void StartReady();
    void CheckReady();
    void StartMatch(bool force);
    void StartKnifeRound();
    void StartLO3(MatchState liveState);
    void FinishLO3();
    void StopMatch();
    void RestartMatch();
    void PauseMatch();
    void UnpauseMatch();
    void SwapTeams();
    void SwapSideScores();
    bool ShouldPreservePlayerScores(MatchState liveState) const;
    void HandleRoundScore(Team team, int score);
    void HandleKnifeRoundScore(Team team, int score);
    void HandleSideSelection(edict_t *entity, bool swapSides);
    void EvaluateMatchProgress();
    void EnterHalftime();
    void EnterOvertime();
    void FinishMatch();
    int GetRequiredReadyCount() const;
    bool ShouldRewriteTeamScoreMessage() const;
    void CacheScoreInfo();
    void SendTeamScore(Team team);
    void SendTeamScoreMessages();
    void SetDisplayedTeamScore(Team team, int score, bool resend);
    void SetDisplayedTeamScores(int terroristScore, int ctScore, bool resend);
    void SyncDisplayedTeamScoresFromMatchScores(bool resend);
    int DisplayedTeamScore(Team team) const;
    void SendScoreInfo(int index);
    void ReplayAllScoreInfo();
    int TeamScoreMessageId();
    int ScoreInfoMessageId();
    int MoneyMessageId();
    const char *EngineTeamScoreName(Team team) const;
    int TeamNumber(Team team) const;
    void EnforceKnifeRoundWeapons();
    void StripKnifeRoundWeapons();
    void RestoreKnifeRoundWeapons();
    edict_t *CreateNamedEntity(const char *classname) const;
    bool StripPlayerWeaponsNative(edict_t *entity) const;
    bool GiveItemNative(edict_t *entity, const char *classname) const;
    bool SetPlayerMoneyNative(edict_t *entity, int money, bool flash);
    bool RemovePlayerC4Native(edict_t *entity) const;
    void EnforcePracticePlayer(edict_t *entity);
    void EnforcePracticeStatePlayers();
    void ResetLivePlayerLoadout(int money);
    void ResetLivePlayerInventory(edict_t *entity);
    void EnforceKnifeRoundPlayerNative(edict_t *entity);
    bool IsKnifeRoundState(MatchState state) const;

    bool DispatchCommand(edict_t *entity, const std::string &raw);
    bool DispatchPlayerCommand(edict_t *entity, const std::string &command);
    bool DispatchAdminCommand(edict_t *entity, const std::string &command);
    bool IsAdmin(edict_t *entity) const;
    bool IsLiveState(MatchState state) const;
    bool IsPracticeState(MatchState state) const;
    bool IsSideSwitchBlocked(MatchState state) const;
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
    const char *TeamName(Team team) const;

    Cvars cvars_{};
    MatchState state_ = MatchState::Disabled;
    MatchState pendingLiveState_ = MatchState::FirstHalf;
    std::array<PlayerInfo, kMaxClients + 1> players_{};
    std::array<std::vector<int>, kMaxClients + 1> savedScoreInfo_{};
    std::set<std::string> admins_{};
    std::vector<ScheduledTask> tasks_{};
    int lo3Step_ = 0;
    int halfRoundCount_ = 0;
    int totalRoundCount_ = 0;
    int overtimeRoundCount_ = 0;
    int terroristScore_ = 0;
    int ctScore_ = 0;
    int displayedTerroristScore_ = 0;
    int displayedCTScore_ = 0;
    int overtimeTerroristStartScore_ = 0;
    int overtimeCTStartScore_ = 0;
    int lastObservedTScore_ = 0;
    int lastObservedCTScore_ = 0;
    bool paused_ = false;
    bool restarting_ = false;
    bool syncingScoreboard_ = false;
    bool knifeRoundCompleted_ = false;
    bool sideSelectionPending_ = false;
    bool suppressCurrentMessage_ = false;
    bool replayingScoreMessages_ = false;
    bool knifeRoundWeaponsEnforced_ = false;
    Team knifeWinner_ = Team::Unknown;
    int teamScoreMessageId_ = 0;
    int scoreInfoMessageId_ = 0;
    int moneyMessageId_ = 0;

    struct MessageCapture {
        int destination = 0;
        int type = 0;
        edict_t *entity = nullptr;
        std::string name;
        std::vector<int> numbers;
        std::vector<std::string> strings;
    } message_{};
};

Plugin &GetPlugin();

} // namespace xmp
