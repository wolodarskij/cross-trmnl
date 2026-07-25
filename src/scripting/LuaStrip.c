/*
** Free the debug arrays a compiled chunk carries: line info, absolute line
** info, local-variable records and upvalue names. Worth ~17 KB on a large
** game, where roughly 46% of the loaded heap is Proto data.
**
** The target state is not invented — it is exactly what `luaU_undump` produces
** for a chunk dumped with strip=1 (lundump.c loadDebug: zero-length arrays,
** null upvalue names). Everything downstream already handles it, because
** loading stripped bytecode has always been a supported path.
**
** Two things are deliberately kept:
**   - Proto::source, one shared string per chunk. Dropping it would save ~30
**     bytes and cost the filename in every error message; keeping it means a
**     stripped script still reports *where* it failed, just not which line.
**   - The Upvaldesc array itself. Only the names are debug data; instack, idx
**     and kind are needed to run the function at all.
*/

#include "lprefix.h"

#include <stddef.h>

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"

#include "LuaStrip.h"

static void stripProto(lua_State* L, Proto* f) {
  int i;
  for (i = 0; i < f->sizep; i++) stripProto(L, f->p[i]);

  /* luaM_freearray asserts (size == 0) == (pointer == NULL), and the GC's
     traverseproto walks these by size — so pointer and size have to be cleared
     together, never one without the other. */
  luaM_freearray(L, f->lineinfo, f->sizelineinfo);
  f->lineinfo = NULL;
  f->sizelineinfo = 0;

  luaM_freearray(L, f->abslineinfo, f->sizeabslineinfo);
  f->abslineinfo = NULL;
  f->sizeabslineinfo = 0;

  luaM_freearray(L, f->locvars, f->sizelocvars);
  f->locvars = NULL;
  f->sizelocvars = 0;

  for (i = 0; i < f->sizeupvalues; i++) f->upvalues[i].name = NULL;
}

void luaStripDebugInfo(lua_State* L, int index) {
  LClosure* cl;
  if (lua_type(L, index) != LUA_TFUNCTION || lua_iscfunction(L, index)) return;
  /* For a collectable value lua_topointer returns the GCObject itself, which
     for a Lua function is the LClosure. */
  cl = (LClosure*)lua_topointer(L, index);
  if (cl == NULL || cl->p == NULL) return;
  stripProto(L, cl->p);
}
