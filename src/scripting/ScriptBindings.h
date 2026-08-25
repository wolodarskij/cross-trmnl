#pragma once

#include <cstddef>
#include <string>

struct lua_State;

namespace scriptbindings {

// Registers the device-facing global tables (screen, input, fs, http, device)
// and overrides print() to route to the on-screen console. Call once, after
// the standard libs are open. The ScriptContext pointer is read from the Lua
// state's extra space (set by ScriptEngine::begin).
void registerAll(lua_State* L);

// The one place the "file is over the SD read cap" wording lives, shared by the
// script loader and by the fs/require bindings. `fileSize` is the file's real
// size; 0 means it could not be determined (an out-of-heap short read).
std::string tooLargeMessage(const char* path, size_t fileSize);

}  // namespace scriptbindings
