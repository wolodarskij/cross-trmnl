#pragma once

// Drops the per-function debug data Lua keeps after compiling a chunk.
//
// The implementation reaches into Lua's internal Proto layout, so it is
// compiled as C against Lua's own headers rather than pulling those into a C++
// translation unit. Only this one-function surface crosses the boundary.

struct lua_State;

#ifdef __cplusplus
extern "C" {
#endif

// `index` must name a Lua function on the stack (typically -1, straight from
// luaL_loadbuffer). Anything else is ignored rather than treated as an error:
// this is an optimisation, and an optimisation that can fail a load is worse
// than one that quietly does nothing.
//
// Recurses into nested prototypes, so one call covers a whole chunk. Safe to
// call on an already-stripped chunk.
void luaStripDebugInfo(struct lua_State* L, int index);

#ifdef __cplusplus
}
#endif
