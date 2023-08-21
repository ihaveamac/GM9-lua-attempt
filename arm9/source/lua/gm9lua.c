#include "gm9lua.h"
#include "ui.h"
#include "language.h"
#ifndef NO_LUA
#include "ff.h"
#include "vff.h"
#include "fsutil.h"

#define DEBUGSP ShowPrompt

typedef struct GM9LuaLoadF {
    int n; // pre-read characters
    FIL f;
    FRESULT res;
    char buff[BUFSIZ];
} GM9LuaLoadF;

// similar to "getF" in lauxlib.c
static const char* GetF(lua_State* L, void* ud, size_t* size) {
    GM9LuaLoadF* lf = (GM9LuaLoadF*)ud;
    UINT br = 0;
    (void)L; // unused
    if (lf->n > 0) { // check for pre-read characters
        *size = lf->n; // return those
        lf->n = 0;
    } else {
        if (fvx_eof(&lf->f)) return NULL;
        lf->res = fvx_read(&lf->f, lf->buff, BUFSIZ, &br);
        *size = (size_t)br;
        if (lf->res != FR_OK) return NULL;
    }
    return lf->buff;
}

// similar to "errfile" in lauxlib.c
static int ErrFile(lua_State* L, const char* what, int fnameindex, FRESULT res) {
    const char* filename = lua_tostring(L, fnameindex) + 1;
    lua_pushfstring(L, "cannot %s %s:\nfatfs error %d", what, filename, res);
    lua_remove(L, fnameindex);
    return LUA_ERRFILE;
}

int LoadLuaFile(lua_State* L, const char* filename) {
    GM9LuaLoadF lf;
    lf.n = 0;
    int status;
    int fnameindex = lua_gettop(L) + 1; // index of filename on the stack
    lua_pushfstring(L, "@%s", filename);
    lf.res = fvx_open(&lf.f, filename, FA_READ | FA_OPEN_EXISTING);
    if (lf.res != FR_OK) return ErrFile(L, "open", fnameindex, lf.res);
    
    status = lua_load(L, GetF, &lf, lua_tostring(L, -1), NULL);
    fvx_close(&lf.f);
    if (lf.res != FR_OK) {
        lua_settop(L, fnameindex);
        return ErrFile(L, "read", fnameindex, lf.res);
    }
    lua_remove(L, fnameindex);
    return status;
}

bool ExecuteLuaScript(const char* path_script) {
    lua_State* L = luaL_newstate();

    lua_pushliteral(L, VERSION);
    lua_setglobal(L, "GM9VERSION");

    int result = LoadLuaFile(L, path_script);
    if (result != LUA_OK) {
        char errstr[BUFSIZ] = {0};
        strlcpy(errstr, lua_tostring(L, -1), BUFSIZ);
        WordWrapString(errstr, 0);
        ShowPrompt(false, "Error during loading:\n%s", errstr);
        lua_close(L);
        return false;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        char errstr[BUFSIZ] = {0};
        strlcpy(errstr, lua_tostring(L, -1), BUFSIZ);
        WordWrapString(errstr, 0);
        ShowPrompt(false, "Error during execution:\n%s", errstr);
        lua_close(L);
        return false;
    }

    lua_close(L);
    return true;
}
#else
// No-Lua version
bool ExecuteLuaScript(const char* path_script) {
    (void)path_script; // unused
    ShowPrompt(false, "%s", STR_LUA_NOT_INCLUDED);
    return false;
}
#endif
