#include "ScriptEngine.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MappedInputManager.h>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "ScriptBindings.h"

namespace {

// How often (in Lua VM instructions) the abort hook fires. Small enough that
// Back feels responsive, large enough that the per-call overhead is noise.
constexpr int kHookCount = 2000;
// Don't poll the ADC button ladder more than ~30x/s from inside the hook.
constexpr unsigned long kPumpIntervalMs = 30;

ScriptContext* ctxOf(lua_State* L) { return *static_cast<ScriptContext**>(lua_getextraspace(L)); }

void countHook(lua_State* L, lua_Debug*) {
  if (ScriptEngine::pump(L)) {
    luaL_error(L, "aborted");  // long-jumps out of the running script
  }
}

// Message handler for lua_pcall: prepend a traceback to the error object.
int msgHandler(lua_State* L) {
  const char* msg = lua_tostring(L, 1);
  if (msg == nullptr) msg = "(non-string error)";
  luaL_traceback(L, L, msg, 1);
  return 1;
}

// Remove base-library globals that can load code from disk or bytecode.
void lockdownGlobals(lua_State* L) {
  static const char* kRemoved[] = {"dofile", "loadfile", "load", "collectgarbage"};
  for (const char* name : kRemoved) {
    lua_pushnil(L);
    lua_setglobal(L, name);
  }
}

}  // namespace

ScriptEngine::ScriptEngine(GfxRenderer& renderer, MappedInputManager& input, std::vector<std::string>& console) {
  ctx_.renderer = &renderer;
  ctx_.input = &input;
  ctx_.console = &console;
}

ScriptEngine::~ScriptEngine() {
  if (L_) lua_close(L_);
}

void* ScriptEngine::alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* self = static_cast<ScriptEngine*>(ud);
  if (nsize == 0) {
    if (ptr) {
      self->memUsed_ -= osize;
      free(ptr);
    }
    return nullptr;
  }
  // Enforce the cap on growth so a script cannot starve the rest of the device.
  const size_t projected = self->memUsed_ - (ptr ? osize : 0) + nsize;
  if (projected > self->memLimit_) return nullptr;  // Lua raises "not enough memory"
  void* np = realloc(ptr, nsize);
  if (!np) return nullptr;
  self->memUsed_ = projected;
  return np;
}

bool ScriptEngine::begin() {
  L_ = lua_newstate(&ScriptEngine::alloc, this);
  if (!L_) {
    LOG_ERR("LUA", "lua_newstate failed (OOM)");
    return false;
  }
  *static_cast<ScriptContext**>(lua_getextraspace(L_)) = &ctx_;

  luaL_openlibs(L_);  // our linit.c: only base/coroutine/table/string/math/utf8
  lockdownGlobals(L_);
  scriptbindings::registerAll(L_);

  lua_sethook(L_, countHook, LUA_MASKCOUNT, kHookCount);
  return true;
}

bool ScriptEngine::pump(lua_State* L) {
  ScriptContext* ctx = ctxOf(L);
  if (!ctx || !ctx->input) return false;
  const unsigned long now = millis();
  if (now - ctx->lastPumpMs < kPumpIntervalMs) return ctx->aborted;
  ctx->lastPumpMs = now;
  ctx->input->update();
  if (ctx->input->wasPressed(MappedInputManager::Button::Back)) {
    ctx->aborted = true;
  }
  return ctx->aborted;
}

bool ScriptEngine::runFile(const std::string& path, std::string& errorOut) {
  if (!L_) {
    errorOut = "engine not initialised";
    return false;
  }

  const String source = Storage.readFile(path.c_str());
  if (source.length() == 0 && !Storage.exists(path.c_str())) {
    errorOut = "cannot read script: " + path;
    return false;
  }

  ctx_.aborted = false;
  ctx_.lastPumpMs = millis();

  // Push the message handler first so pcall can reference it by index.
  lua_pushcfunction(L_, msgHandler);
  const int msgh = lua_gettop(L_);

  // Chunk name "@file" makes traceback lines read as file:line.
  const std::string chunkName = "@" + path;
  int status = luaL_loadbuffer(L_, source.c_str(), source.length(), chunkName.c_str());
  if (status == LUA_OK) {
    status = lua_pcall(L_, 0, 0, msgh);
  }

  bool ok = (status == LUA_OK);
  if (!ok) {
    const char* msg = lua_tostring(L_, -1);
    errorOut = msg ? msg : "unknown error";
    lua_pop(L_, 1);  // error object
  }
  lua_pop(L_, 1);  // message handler
  return ok;
}
