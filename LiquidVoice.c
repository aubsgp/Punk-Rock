#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <windows.h>

#include "lua.h"

//------------------------
//lua_L functions and structs are missing from the desmume's lua dll, so if we commit to compiling against it, we have to implement them ourselves. 
typedef struct luaL_Reg {
    const char *name;
    lua_CFunction func;
} luaL_Reg;

static int luaL_error(lua_State *L, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    return lua_error(L);
}

static lua_Integer luaL_checkinteger(lua_State *L, int idx)
{
    if (!lua_isnumber(L, idx))
    {
        lua_pushstring(L, "expected number argument");
        lua_error(L);
    }
    return lua_tointeger(L, idx);
}

static char *luaL_checkstring(lua_State *L, int idx)
{
    if (!lua_isstring(L, idx))
    {
        lua_pushstring(L, "expected string argument");
        lua_error(L);
    }
    return (char *)lua_tostring(L, idx);
}

static void luaL_register(lua_State *L, const char *name, const luaL_Reg *lib)
{
    lua_createtable(L, 0, 0);
    for (int i = 0; lib[i].name != NULL; i++)
    {
        lua_pushcclosure(L, lib[i].func, 0);
        lua_setfield(L, -2, lib[i].name);
    }
    if (name)
    {
        lua_pushvalue(L, -1);
        lua_setfield(L, -10002, name);  // -10002 is LUA_GLOBALSINDEX in Lua 5.1
    }
}
// End of lua_L implementations.
//------------------------

static HANDLE pipe = INVALID_HANDLE_VALUE;
static int write_pipe(lua_State *L)
{
    uint8_t command = luaL_checkinteger(L, 1);
    uint8_t target  = luaL_checkinteger(L, 2); // 0 = player_field, 1 = player_bgm, 2 = global, i.e. "pause independently of which player is playing".
    uint16_t value  = luaL_checkinteger(L, 3);
    uint32_t to_write = command << 24 | target << 16 | value;

    if (pipe == INVALID_HANDLE_VALUE)
    {
        pipe = CreateFileW(
            L"\\\\.\\pipe\\round", // lpFileName
            GENERIC_WRITE,         // dwDesiredAccess
            0,                     // dwShareMode (don't share)
            NULL,                  // lpSecurityAttributes (default security)
            OPEN_EXISTING,         // dwCreationDisposition (file should already exist from PunkRock.cpp)
            0,                     // dwFlagsAndAttributes (no special flags)
            NULL                   // hTemplateFile (default template)
        );
    }

    if (pipe == INVALID_HANDLE_VALUE)
    {
        lua_pushboolean(L, false); // Tell the lua script the setting failed, and it should try again later.
    }
    else
    {    
        DWORD written;
        if (!WriteFile(pipe, &to_write, sizeof(to_write), &written, NULL))
        {
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            lua_pushboolean(L, false);
        }
        else
        {
            lua_pushboolean(L, true);
        }
    }

    return 1;
}

static const luaL_Reg liquidVoiceLib[] = {
    {"write_pipe", write_pipe},
    {NULL, NULL}
};

__declspec(dllexport) int luaopen_LiquidVoice(lua_State *L)
{
    luaL_register(L, "liquidVoice", liquidVoiceLib);
    return 1;
}
