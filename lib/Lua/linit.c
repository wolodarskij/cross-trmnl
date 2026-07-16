/*
** cross-trmnl replacement for the stock linit.c.
**
** Opens only the standard libraries that are safe to expose to user scripts
** on the device. Deliberately omitted:
**   - package/require/loadlib (loadlib.c not vendored) — no dynamic loading
**   - io  (liolib.c not vendored)                      — no raw file handles
**   - os  (loslib.c not vendored)                      — no execute/exit/remove
**   - debug (ldblib.c not vendored)                    — no introspection/hooks
** The device-facing API (screen/input/fs/http/device) is registered separately
** by ScriptBindings, and dangerous base globals (dofile/loadfile/load) are
** removed there.
*/

#define linit_c
#define LUA_LIB

#include "lprefix.h"

#include <stddef.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static const luaL_Reg loadedlibs[] = {
  {LUA_GNAME, luaopen_base},
  {LUA_COLIBNAME, luaopen_coroutine},
  {LUA_TABLIBNAME, luaopen_table},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_MATHLIBNAME, luaopen_math},
  {LUA_UTF8LIBNAME, luaopen_utf8},
  {NULL, NULL}
};

LUALIB_API void luaL_openlibs (lua_State *L) {
  const luaL_Reg *lib;
  for (lib = loadedlibs; lib->func; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1);  /* remove lib */
  }
}
