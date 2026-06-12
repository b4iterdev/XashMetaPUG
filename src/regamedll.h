#pragma once

#include <regamedll_api.h>

namespace xmp {

extern IReGameApi *g_ReGameApi;
extern const ReGameFuncs_t *g_ReGameFuncs;
extern IReGameHookchains *g_ReGameHookchains;

bool ReGameDLL_Init();
bool ReGameDLL_Stop();

bool OnRoundEnd(IReGameHook_RoundEnd *chain, int winStatus, ScenarioEventEndRound event, float tmDelay);
void OnCSGameRules_RestartRound(IReGameHook_CSGameRules_RestartRound *chain);
void OnCSGameRules_OnRoundFreezeEnd(IReGameHook_CSGameRules_OnRoundFreezeEnd *chain);
void OnCBasePlayer_OnSpawnEquip(IReGameHook_CBasePlayer_OnSpawnEquip *chain, CBasePlayer *player, bool addDefault, bool equipGame);
void OnCSGameRules_PlayerSpawn(IReGameHook_CSGameRules_PlayerSpawn *chain, CBasePlayer *player);
void OnCBasePlayer_Killed(IReGameHook_CBasePlayer_Killed *chain, CBasePlayer *player, entvars_t *pevAttacker, int iGib);
int OnCBasePlayer_TakeDamage(IReGameHook_CBasePlayer_TakeDamage *chain, CBasePlayer *pThis, entvars_t *pevInflictor, entvars_t *pevAttacker, float &flDamage, int bitsDamageType);
BOOL OnHandleMenu_ChooseTeam(IReGameHook_HandleMenu_ChooseTeam *chain, CBasePlayer *player, int slot);
void OnCBasePlayer_SwitchTeam(IReGameHook_CBasePlayer_SwitchTeam *chain, CBasePlayer *player);
bool OnCBasePlayer_GetIntoGame(IReGameHook_CBasePlayer_GetIntoGame *chain, CBasePlayer *player);
void OnInternalCommand(IReGameHook_InternalCommand *chain, edict_t *pEntity, const char *pcmd, const char *parg1);
void OnCSGameRules_PlayerKilled(IReGameHook_CSGameRules_PlayerKilled *chain, CBasePlayer *pVictim, entvars_t *pevKiller, entvars_t *pevInflictor);
void OnCBasePlayer_AddAccount(IReGameHook_CBasePlayer_AddAccount *chain, CBasePlayer *player, int Amount, RewardType Type, bool TrackChange);
void OnCBasePlayer_GiveShield(IReGameHook_CBasePlayer_GiveShield *chain, CBasePlayer *player, bool deploy);
bool OnCBasePlayer_HasRestrictItem(IReGameHook_CBasePlayer_HasRestrictItem *chain, CBasePlayer *player, ItemID item, ItemRestType type);
CGameRules *OnInstallGameRules(IReGameHook_InstallGameRules *chain);

} // namespace xmp
