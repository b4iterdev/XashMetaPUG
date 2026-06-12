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
#include <ctime>
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
#include <regamedll_api.h>
#include <gamerules.h>

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
    int pendingClassSlot = 0;
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
    cvar_t timeoutTime{};
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

    // ReGameDLL hook callbacks
    void OnRoundEnd(int winStatus);
    void OnRoundRestart();
    void OnRoundFreezeEnd();
    // Returns true if equipment was handled (caller should skip chain->callNext).
    bool OnPlayerSpawnEquip(CBasePlayer *player, bool addDefault, bool equipGame);
    void OnPlayerSpawn(CBasePlayer *player);
    void OnPlayerKilled(CBasePlayer *player, entvars_t *pevAttacker, int iGib);
    void OnPlayerTakeDamage(CBasePlayer *pThis, entvars_t *pevAttacker, float flDamage, int bitsDamageType);
    BOOL OnChooseTeam(IReGameHook_HandleMenu_ChooseTeam *chain, CBasePlayer *player, int slot);
    void OnPlayerSwitchTeam(CBasePlayer *player);
    void OnPlayerGetIntoGame(CBasePlayer *player);
    void OnPlayerAddAccount(CBasePlayer *player, int Amount, RewardType Type, bool TrackChange);
    void OnCSPlayerKilled(CBasePlayer *pVictim, entvars_s *pevKiller, entvars_s *pevInflictor);
    bool OnInternalCommand(edict_t *pEntity, const char *pcmd, const char *parg1);
    bool OnPlayerGiveShield(CBasePlayer *player, bool deploy);
    bool OnPlayerHasRestrictItem(CBasePlayer *player, ItemID item, ItemRestType type);

private:
    void RegisterCvars();
    void RegisterCvar(cvar_t &cvar, const char *name, const char *value, int flags = FCVAR_EXTDLL);
    void LoadAdmins();
    void ResetMatch(bool keepWarmup);
    void SetState(MatchState next);
    void ExecuteStateConfig(MatchState state);
    void ApplyStateRules(MatchState state);
    void ApplyPracticeStateRules();
    void ApplyKnifeRoundStateRules();
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
    void TimeoutMatch();
    void TechTimeout();
    void RequestPause(const char *caller, int duration, bool isTech);
    void ApplyPause();
    void SwapTeams();
    void AssignRandomModelForTeam(edict_t *entity, Team team);
    int RandomClassSlotForTeam(Team team) const;
    void QueueRandomClassSelection(int index, edict_t *entity, Team team);
    void ForcePendingClassSelection(int index);
    void ClearPendingClassSelection(int index);
    void SwapSideScores();
    bool ShouldPreservePlayerScores(MatchState liveState) const;
    void HandleRoundScore(Team team, int score);
    void HandleKnifeRoundScore(Team team, int score);
    void HandleSideSelection(edict_t *entity, bool swapSides);
    void EvaluateMatchProgress();
    void EnterHalftime();
    void EnterOvertime();
    void EnterOvertimeSideSwap();
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
    void EnforceKnifeRoundWeapons(bool announce = true);
    void StripKnifeRoundWeapons();
    void RestoreKnifeRoundWeapons();
    void KillRandomPlayer();
    edict_t *CreateNamedEntity(const char *classname) const;
    bool StripPlayerWeaponsNative(edict_t *entity) const;
    bool GiveItemNative(edict_t *entity, const char *classname) const;
    bool SetPlayerMoneyNative(edict_t *entity, int money, bool flash);
    bool RemovePlayerC4Native(edict_t *entity) const;
    bool RemovePlayerShieldNative(edict_t *entity) const;
    void EnforceTacticalShieldRestriction();
    void EnforcePracticePlayer(edict_t *entity);
    void EnforcePracticeStatePlayers();
    void ResetLivePlayerLoadout(int money);
    void ResetLivePlayerInventory(edict_t *entity);
    void EnforceKnifeRoundPlayerNative(edict_t *entity);
    bool IsKnifeRoundState(MatchState state) const;

    bool DispatchCommand(edict_t *entity, std::string raw);
    bool DispatchPlayerCommand(edict_t *entity, const std::string &command);
    bool DispatchAdminCommand(edict_t *entity, const std::string &command);
    bool IsAdmin(edict_t *entity) const;
    bool IsLiveState(MatchState state) const;
    bool IsPracticeState(MatchState state) const;
    bool IsSideSwitchBlocked(MatchState state) const;
    bool IsTacticalShieldCommand(edict_t *entity, const char *command, const char *arg) const;
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
    bool overtimeSidesSwapped_ = false;
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
    bool restoringScores_ = false;
    bool recording_ = false;
    bool techPaused_ = false;
    bool halftimeScoresSaved_ = false;
    bool pauseRequested_ = false;
    int pauseDuration_ = 0;
    float savedFreezeTime_ = 0.0f;
    float savedBuyTime_ = 0.0f;
    cvar_t *mpFreezeTimeCvar_ = nullptr;
    cvar_t *mpBuyTimeCvar_ = nullptr;
    int roundTimeMsgId_ = 0;
    std::set<int> techUnpauseVotes_{};
    std::string teamAName_;
    std::string teamBName_;
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
