#include "plugin.h"

enginefuncs_t g_engfuncs;
globalvars_t *gpGlobals = nullptr;
meta_globals_t *gpMetaGlobals = nullptr;
gamedll_funcs_t *gpGamedllFuncs = nullptr;
mutil_funcs_t *gpMetaUtilFuncs = nullptr;

namespace {
META_FUNCTIONS g_metaFunctions;
DLL_FUNCTIONS g_dllFunctions;
enginefuncs_t g_engineFunctions;

BOOL DLL_ClientConnect(edict_t *entity, const char *name, const char *address, char rejectReason[128])
{
    if (!xmp::GetPlugin().OnClientConnect(entity, name)) {
        RETURN_META_VALUE(MRES_SUPERCEDE, FALSE);
    }
    RETURN_META_VALUE(MRES_IGNORED, TRUE);
}

void DLL_ServerActivate(edict_t *edictList, int edictCount, int clientMax)
{
    xmp::GetPlugin().OnServerActivate();
    RETURN_META(MRES_IGNORED);
}

void DLL_ServerDeactivate()
{
    xmp::GetPlugin().OnServerDeactivate();
    RETURN_META(MRES_IGNORED);
}

void DLL_StartFrame()
{
    xmp::GetPlugin().OnStartFrame();
    RETURN_META(MRES_IGNORED);
}

void DLL_ClientPutInServer(edict_t *entity)
{
    xmp::GetPlugin().OnClientPutInServer(entity);
    RETURN_META(MRES_IGNORED);
}

void DLL_ClientDisconnect(edict_t *entity)
{
    xmp::GetPlugin().OnClientDisconnect(entity);
    RETURN_META(MRES_IGNORED);
}

void DLL_ClientCommand(edict_t *entity)
{
    if (xmp::GetPlugin().OnClientCommand(entity)) {
        RETURN_META(MRES_SUPERCEDE);
    }
    RETURN_META(MRES_IGNORED);
}

void ENG_MessageBegin(int destination, int type, const float *origin, edict_t *entity)
{
    RETURN_META(xmp::GetPlugin().OnMessageBegin(destination, type, origin, entity) ? MRES_SUPERCEDE : MRES_IGNORED);
}

void ENG_WriteByte(int value) { RETURN_META(xmp::GetPlugin().OnWriteByte(value) ? MRES_SUPERCEDE : MRES_IGNORED); }
void ENG_WriteChar(int value) { RETURN_META(xmp::GetPlugin().OnWriteChar(value) ? MRES_SUPERCEDE : MRES_IGNORED); }
void ENG_WriteShort(int value) { RETURN_META(xmp::GetPlugin().OnWriteShort(value) ? MRES_SUPERCEDE : MRES_IGNORED); }
void ENG_WriteLong(int value) { RETURN_META(xmp::GetPlugin().OnWriteLong(value) ? MRES_SUPERCEDE : MRES_IGNORED); }
void ENG_WriteString(const char *value) { RETURN_META(xmp::GetPlugin().OnWriteString(value) ? MRES_SUPERCEDE : MRES_IGNORED); }

void ENG_MessageEnd()
{
    RETURN_META(xmp::GetPlugin().OnMessageEnd() ? MRES_SUPERCEDE : MRES_IGNORED);
}
} // namespace

plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,
    "XashMetaPUG",
    "0.1.0",
    __DATE__,
    "b4iterdev/Sisyphus",
    "https://github.com/local/XashMetaPUG",
    "XMP",
    PT_STARTUP,
    PT_ANYTIME,
};

C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t *engineFunctions, globalvars_t *globals)
{
    std::memcpy(&g_engfuncs, engineFunctions, sizeof(enginefuncs_t));
    gpGlobals = globals;
}

C_DLLEXPORT int Meta_Query(char *interfaceVersion, plugin_info_t **pluginInfo, mutil_funcs_t *metaUtilFuncs)
{
    *pluginInfo = &Plugin_info;
    gpMetaUtilFuncs = metaUtilFuncs;
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS *functionTable, meta_globals_t *metaGlobals, gamedll_funcs_t *gameDllFuncs)
{
    std::memset(&g_metaFunctions, 0, sizeof(g_metaFunctions));
    g_metaFunctions.pfnGetEntityAPI2 = GetEntityAPI2;
    g_metaFunctions.pfnGetEngineFunctions = GetEngineFunctions;
    std::memcpy(functionTable, &g_metaFunctions, sizeof(g_metaFunctions));

    gpMetaGlobals = metaGlobals;
    gpGamedllFuncs = gameDllFuncs;
    xmp::GetPlugin().OnMetaAttach();
    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason)
{
    xmp::GetPlugin().OnServerDeactivate();
    return TRUE;
}

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS *functionTable, int *interfaceVersion)
{
    std::memset(&g_dllFunctions, 0, sizeof(g_dllFunctions));
    g_dllFunctions.pfnClientConnect = DLL_ClientConnect;
    g_dllFunctions.pfnServerActivate = DLL_ServerActivate;
    g_dllFunctions.pfnServerDeactivate = DLL_ServerDeactivate;
    g_dllFunctions.pfnStartFrame = DLL_StartFrame;
    g_dllFunctions.pfnClientPutInServer = DLL_ClientPutInServer;
    g_dllFunctions.pfnClientDisconnect = DLL_ClientDisconnect;
    g_dllFunctions.pfnClientCommand = DLL_ClientCommand;
    std::memcpy(functionTable, &g_dllFunctions, sizeof(g_dllFunctions));
    return TRUE;
}

C_DLLEXPORT int GetEngineFunctions(enginefuncs_t *engineFunctions, int *interfaceVersion)
{
    std::memset(&g_engineFunctions, 0, sizeof(g_engineFunctions));
    g_engineFunctions.pfnMessageBegin = ENG_MessageBegin;
    g_engineFunctions.pfnMessageEnd = ENG_MessageEnd;
    g_engineFunctions.pfnWriteByte = ENG_WriteByte;
    g_engineFunctions.pfnWriteChar = ENG_WriteChar;
    g_engineFunctions.pfnWriteShort = ENG_WriteShort;
    g_engineFunctions.pfnWriteLong = ENG_WriteLong;
    g_engineFunctions.pfnWriteString = ENG_WriteString;
    std::memcpy(engineFunctions, &g_engineFunctions, sizeof(g_engineFunctions));
    return TRUE;
}
