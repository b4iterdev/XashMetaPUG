#include "plugin.h"
#include "regamedll.h"

#include <interface.h>
#include <dlfcn.h>

// Sys_LoadModule / Sys_GetFactory implementations for platforms where
// the engine does not export them (Xash3D, non-HLDS engines).
// Provides the same interface as Valve's interface.cpp.
namespace {

CSysModule *SysLoadModule(const char *pModuleName)
{
    if (!pModuleName || !*pModuleName)
        return nullptr;

    char abspath[1024];
    void *handle = nullptr;

    if (pModuleName[0] == '/') {
        handle = dlopen(pModuleName, RTLD_NOW);
    } else {
        char cwd[1024];
        if (!getcwd(cwd, sizeof(cwd)))
            return nullptr;
        if (cwd[strlen(cwd) - 1] == '/')
            cwd[strlen(cwd) - 1] = '\0';
        snprintf(abspath, sizeof(abspath), "%s/%s", cwd, pModuleName);
        handle = dlopen(abspath, RTLD_NOW);
    }

    if (!handle) {
        snprintf(abspath, sizeof(abspath), "%s.so", pModuleName);
        handle = dlopen(abspath, RTLD_NOW);
    }

    return reinterpret_cast<CSysModule *>(handle);
}

void SysUnloadModule(CSysModule *pModule)
{
    if (pModule)
        dlclose(reinterpret_cast<void *>(pModule));
}

CreateInterfaceFn SysGetFactory(CSysModule *pModule)
{
    if (!pModule)
        return nullptr;
    return reinterpret_cast<CreateInterfaceFn>(
        dlsym(reinterpret_cast<void *>(pModule), CREATEINTERFACE_PROCNAME));
}

}

namespace xmp {

IReGameApi *g_ReGameApi = nullptr;
const ReGameFuncs_t *g_ReGameFuncs = nullptr;
IReGameHookchains *g_ReGameHookchains = nullptr;

bool ReGameDLL_Init()
{
    const char *gameDllPath = GET_GAME_INFO(PLID, GINFO_DLL_FULLPATH);
    if (!gameDllPath || !*gameDllPath) {
        g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL: GET_GAME_INFO failed (not running under Metamod?)\n");
        return false;
    }

    CSysModule *gameModule = SysLoadModule(gameDllPath);
    if (!gameModule) {
        g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL: failed to load GameDLL module: %s\n", gameDllPath);
        return false;
    }

    CreateInterfaceFn ifaceFactory = SysGetFactory(gameModule);
    if (!ifaceFactory) {
        g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL: failed to get interface factory from GameDLL\n");
        SysUnloadModule(gameModule);
        return false;
    }

    int retCode = 0;
    g_ReGameApi = reinterpret_cast<IReGameApi *>(
        ifaceFactory(VRE_GAMEDLL_API_VERSION, &retCode));
    if (!g_ReGameApi) {
        g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL: IReGameApi not found (retCode=%d). "
            "Server may be running vanilla mp.dll instead of ReGameDLL.\n", retCode);
        return false;
    }

    int major = g_ReGameApi->GetMajorVersion();
    int minor = g_ReGameApi->GetMinorVersion();
    if (major != REGAMEDLL_API_VERSION_MAJOR) {
        g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL: major version mismatch: expected %d, got %d\n",
            REGAMEDLL_API_VERSION_MAJOR, major);
        return false;
    }
    if (minor < REGAMEDLL_API_VERSION_MINOR) {
        g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL: minor version too old: need >=%d, got %d\n",
            REGAMEDLL_API_VERSION_MINOR, minor);
        return false;
    }

    g_ReGameFuncs = g_ReGameApi->GetFuncs();
    g_ReGameHookchains = g_ReGameApi->GetHookchains();

    if (!g_ReGameApi->BGetICSEntity(CSENTITY_API_INTERFACE_VERSION)) {
        g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL: CCSEntity API version '%s' not found\n",
            CSENTITY_API_INTERFACE_VERSION);
        return false;
    }

    g_ReGameHookchains->RoundEnd()->registerHook(OnRoundEnd);
    g_ReGameHookchains->CSGameRules_RestartRound()->registerHook(OnCSGameRules_RestartRound);
    g_ReGameHookchains->CSGameRules_OnRoundFreezeEnd()->registerHook(OnCSGameRules_OnRoundFreezeEnd);
    g_ReGameHookchains->CBasePlayer_OnSpawnEquip()->registerHook(OnCBasePlayer_OnSpawnEquip);
    g_ReGameHookchains->CSGameRules_PlayerSpawn()->registerHook(OnCSGameRules_PlayerSpawn);
    g_ReGameHookchains->CBasePlayer_Killed()->registerHook(OnCBasePlayer_Killed);
    g_ReGameHookchains->CBasePlayer_TakeDamage()->registerHook(OnCBasePlayer_TakeDamage);
    g_ReGameHookchains->HandleMenu_ChooseTeam()->registerHook(OnHandleMenu_ChooseTeam);
    g_ReGameHookchains->CBasePlayer_SwitchTeam()->registerHook(OnCBasePlayer_SwitchTeam);
    g_ReGameHookchains->CBasePlayer_GetIntoGame()->registerHook(OnCBasePlayer_GetIntoGame);
    g_ReGameHookchains->CBasePlayer_AddAccount()->registerHook(OnCBasePlayer_AddAccount);
    g_ReGameHookchains->CBasePlayer_GiveShield()->registerHook(OnCBasePlayer_GiveShield);
    g_ReGameHookchains->CBasePlayer_HasRestrictItem()->registerHook(OnCBasePlayer_HasRestrictItem);
    g_ReGameHookchains->InstallGameRules()->registerHook(OnInstallGameRules);
    g_ReGameHookchains->CSGameRules_PlayerKilled()->registerHook(OnCSGameRules_PlayerKilled);
    g_ReGameHookchains->InternalCommand()->registerHook(OnInternalCommand);

    g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL initialized (v%d.%d)\n", major, minor);
    return true;
}

bool ReGameDLL_Stop()
{
    if (!g_ReGameHookchains)
        return false;

    g_ReGameHookchains->RoundEnd()->unregisterHook(OnRoundEnd);
    g_ReGameHookchains->CSGameRules_RestartRound()->unregisterHook(OnCSGameRules_RestartRound);
    g_ReGameHookchains->CSGameRules_OnRoundFreezeEnd()->unregisterHook(OnCSGameRules_OnRoundFreezeEnd);
    g_ReGameHookchains->CBasePlayer_OnSpawnEquip()->unregisterHook(OnCBasePlayer_OnSpawnEquip);
    g_ReGameHookchains->CSGameRules_PlayerSpawn()->unregisterHook(OnCSGameRules_PlayerSpawn);
    g_ReGameHookchains->CBasePlayer_Killed()->unregisterHook(OnCBasePlayer_Killed);
    g_ReGameHookchains->CBasePlayer_TakeDamage()->unregisterHook(OnCBasePlayer_TakeDamage);
    g_ReGameHookchains->HandleMenu_ChooseTeam()->unregisterHook(OnHandleMenu_ChooseTeam);
    g_ReGameHookchains->CBasePlayer_SwitchTeam()->unregisterHook(OnCBasePlayer_SwitchTeam);
    g_ReGameHookchains->CBasePlayer_GetIntoGame()->unregisterHook(OnCBasePlayer_GetIntoGame);
    g_ReGameHookchains->CBasePlayer_AddAccount()->unregisterHook(OnCBasePlayer_AddAccount);
    g_ReGameHookchains->CBasePlayer_GiveShield()->unregisterHook(OnCBasePlayer_GiveShield);
    g_ReGameHookchains->CBasePlayer_HasRestrictItem()->unregisterHook(OnCBasePlayer_HasRestrictItem);
    g_ReGameHookchains->InstallGameRules()->unregisterHook(OnInstallGameRules);
    g_ReGameHookchains->CSGameRules_PlayerKilled()->unregisterHook(OnCSGameRules_PlayerKilled);
    g_ReGameHookchains->InternalCommand()->unregisterHook(OnInternalCommand);

    g_ReGameApi = nullptr;
    g_ReGameFuncs = nullptr;
    g_ReGameHookchains = nullptr;

    g_engfuncs.pfnAlertMessage(at_logged, "[XMP] ReGameDLL shut down\n");
    return true;
}

bool OnRoundEnd(IReGameHook_RoundEnd *chain, int winStatus, ScenarioEventEndRound event, float tmDelay)
{
    GetPlugin().OnRoundEnd(winStatus);
    return chain->callNext(winStatus, event, tmDelay);
}

void OnCSGameRules_RestartRound(IReGameHook_CSGameRules_RestartRound *chain)
{
    GetPlugin().OnRoundRestart();
    chain->callNext();
}

void OnCSGameRules_OnRoundFreezeEnd(IReGameHook_CSGameRules_OnRoundFreezeEnd *chain)
{
    chain->callNext();
    GetPlugin().OnRoundFreezeEnd();
}

void OnCBasePlayer_OnSpawnEquip(IReGameHook_CBasePlayer_OnSpawnEquip *chain,
    CBasePlayer *player, bool addDefault, bool equipGame)
{
    // If the plugin already handled equipment (knife-round enforcement),
    // skip the original which would re-add a free pistol.
    if (!GetPlugin().OnPlayerSpawnEquip(player, addDefault, equipGame)) {
        chain->callNext(player, addDefault, equipGame);
    }
}

void OnCSGameRules_PlayerSpawn(IReGameHook_CSGameRules_PlayerSpawn *chain,
    CBasePlayer *player)
{
    chain->callNext(player);
    GetPlugin().OnPlayerSpawn(player);
}

void OnCBasePlayer_Killed(IReGameHook_CBasePlayer_Killed *chain,
    CBasePlayer *player, entvars_t *pevAttacker, int iGib)
{
    GetPlugin().OnPlayerKilled(player, pevAttacker, iGib);
    chain->callNext(player, pevAttacker, iGib);
}

int OnCBasePlayer_TakeDamage(IReGameHook_CBasePlayer_TakeDamage *chain,
    CBasePlayer *pThis, entvars_t *pevInflictor, entvars_t *pevAttacker,
    float &flDamage, int bitsDamageType)
{
    GetPlugin().OnPlayerTakeDamage(pThis, pevAttacker, flDamage, bitsDamageType);
    return chain->callNext(pThis, pevInflictor, pevAttacker, flDamage, bitsDamageType);
}

BOOL OnHandleMenu_ChooseTeam(IReGameHook_HandleMenu_ChooseTeam *chain,
    CBasePlayer *player, int slot)
{
    return GetPlugin().OnChooseTeam(chain, player, slot);
}

void OnCBasePlayer_SwitchTeam(IReGameHook_CBasePlayer_SwitchTeam *chain,
    CBasePlayer *player)
{
    chain->callNext(player);
    GetPlugin().OnPlayerSwitchTeam(player);
}

bool OnCBasePlayer_GetIntoGame(IReGameHook_CBasePlayer_GetIntoGame *chain,
    CBasePlayer *player)
{
    bool ret = chain->callNext(player);
    GetPlugin().OnPlayerGetIntoGame(player);
    return ret;
}

void OnCBasePlayer_AddAccount(IReGameHook_CBasePlayer_AddAccount *chain,
    CBasePlayer *player, int Amount, RewardType Type, bool TrackChange)
{
    GetPlugin().OnPlayerAddAccount(player, Amount, Type, TrackChange);
    chain->callNext(player, Amount, Type, TrackChange);
}

void OnCBasePlayer_GiveShield(IReGameHook_CBasePlayer_GiveShield *chain,
    CBasePlayer *player, bool deploy)
{
    if (GetPlugin().OnPlayerGiveShield(player, deploy)) {
        return;
    }
    chain->callNext(player, deploy);
}

bool OnCBasePlayer_HasRestrictItem(IReGameHook_CBasePlayer_HasRestrictItem *chain,
    CBasePlayer *player, ItemID item, ItemRestType type)
{
    if (GetPlugin().OnPlayerHasRestrictItem(player, item, type)) {
        return true;
    }
    return chain->callNext(player, item, type);
}

CGameRules *OnInstallGameRules(IReGameHook_InstallGameRules *chain)
{
    return chain->callNext();
}

void OnCSGameRules_PlayerKilled(IReGameHook_CSGameRules_PlayerKilled *chain,
    CBasePlayer *pVictim, entvars_s *pevKiller, entvars_s *pevInflictor)
{
    chain->callNext(pVictim, pevKiller, pevInflictor);
    GetPlugin().OnCSPlayerKilled(pVictim, pevKiller, pevInflictor);
}

void OnInternalCommand(IReGameHook_InternalCommand *chain,
    edict_t *pEntity, const char *pcmd, const char *parg1)
{
    if (GetPlugin().OnInternalCommand(pEntity, pcmd, parg1))
        return;
    chain->callNext(pEntity, pcmd, parg1);
}

} // namespace xmp
