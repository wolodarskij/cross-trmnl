#pragma once

struct lua_State;

namespace scriptbindings {

// Registers the device-facing global tables (screen, input, fs, http, device)
// and overrides print() to route to the on-screen console. Call once, after
// the standard libs are open. The ScriptContext pointer is read from the Lua
// state's extra space (set by ScriptEngine::begin).
void registerAll(lua_State* L);

}  // namespace scriptbindings
