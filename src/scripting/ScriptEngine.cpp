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

#include "CrossPointSettings.h"
#include "LuaStrip.h"
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
  ctx_.engine = this;
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
  // Returning nullptr is not fatal: Lua responds by running an emergency full
  // collection and retrying, and only raises LUA_ERRMEM if that also fails.
  const size_t projected = self->memUsed_ - (ptr ? osize : 0) + nsize;
  if (projected > self->memLimit_) return nullptr;
  void* np = realloc(ptr, nsize);
  if (!np) return nullptr;
  self->memUsed_ = projected;
  if (projected > self->memPeak_) self->memPeak_ = projected;
  return np;
}

bool ScriptEngine::begin() {
  // Re-clamp the reserve read from settings. JsonSettingsIO already range-checks
  // it on load, but settings.json is a plain file on a removable card: a value
  // that reaches here out of range would hand a script the heap the failure
  // screen needs to report that very failure.
  uint8_t reserveKb = SETTINGS.scriptHeapReserveKb;
  if (reserveKb < CrossPointSettings::MIN_SCRIPT_HEAP_RESERVE_KB) {
    reserveKb = CrossPointSettings::MIN_SCRIPT_HEAP_RESERVE_KB;
  } else if (reserveKb > CrossPointSettings::MAX_SCRIPT_HEAP_RESERVE_KB) {
    reserveKb = CrossPointSettings::MAX_SCRIPT_HEAP_RESERVE_KB;
  }
  heapReserve_ = static_cast<size_t>(reserveKb) * 1024;

  // Size the budget from the heap that is actually free right now, rather than
  // hard-coding one number for every device state. Whatever is left after the
  // reserve is a script's to use, clamped to a sane band so a momentarily
  // fragmented heap cannot shrink the budget below what a real game needs, and
  // a generous one cannot let a script claim everything.
  const size_t freeHeap = ESP.getFreeHeap();
  size_t budget = (freeHeap > heapReserve_) ? (freeHeap - heapReserve_) : 0;
  // Remember which way the clamp went. "96 KB allowed" on its own is unreadable:
  // it looks like a fixed cap when it is actually the floor overriding a heap
  // that had nothing to give. Saying which happened is the difference between a
  // number and a diagnosis.
  memClamp_ = (budget < kMemFloor) ? Clamp::Floor : (budget > kMemCeiling ? Clamp::Ceiling : Clamp::None);
  if (budget < kMemFloor) budget = kMemFloor;
  if (budget > kMemCeiling) budget = kMemCeiling;
  memLimit_ = budget;
  memUsed_ = 0;
  memPeak_ = 0;
  freeHeapAtStart_ = freeHeap;
  LOG_INF("LUA", "memory budget %u bytes (free heap %u, reserve %u, clamp %s)", (unsigned)memLimit_,
          (unsigned)freeHeap, (unsigned)heapReserve_, clampName());

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

  // Long-hold Back always force-quits (independent of script soft-Back handling).
  // Short press (release before the hold threshold) queues a soft "back" event.
  const bool down = ctx->input->isPressed(MappedInputManager::Button::Back);
  if (down) {
    if (ctx->backDownMs == 0) {
      ctx->backDownMs = now;
    } else if (now - ctx->backDownMs >= kBackHoldExitMs) {
      ctx->aborted = true;
    }
  } else if (ctx->backDownMs != 0) {
    if (!ctx->aborted && (now - ctx->backDownMs) < kBackHoldExitMs) {
      ctx->backSoftPending = true;
    }
    ctx->backDownMs = 0;
  }
  return ctx->aborted;
}

bool ScriptEngine::runFile(const std::string& path, std::string& errorOut) {
  if (!L_) {
    errorOut = "engine not initialised";
    return false;
  }

  bool truncated = false;
  size_t fileSize = 0;
  const String source = Storage.readFile(path.c_str(), &truncated, &fileSize);
  if (source.length() == 0 && !Storage.exists(path.c_str())) {
    errorOut = "cannot read script: " + path;
    return false;
  }
  // Compiling a file the SD layer cut short reports a syntax error at whatever
  // line the cut landed on, which sends the author hunting a bug that is not
  // there. Fail on the real reason instead, before the parser ever sees it.
  if (truncated) {
    errorOut = scriptbindings::tooLargeMessage(path.c_str(), fileSize);
    return false;
  }

  ctx_.aborted = false;
  ctx_.backDownMs = 0;
  ctx_.backSoftPending = false;
  ctx_.lastPumpMs = millis();

  // Push the message handler first so pcall can reference it by index.
  lua_pushcfunction(L_, msgHandler);
  const int msgh = lua_gettop(L_);

  // Chunk name "@file" makes traceback lines read as file:line.
  const std::string chunkName = "@" + path;
  int status = luaL_loadbuffer(L_, source.c_str(), source.length(), chunkName.c_str());
  if (status == LUA_OK) {
    // Before the chunk runs, not after: the debug arrays are dead weight for
    // its whole lifetime, and the point is to have that weight gone while the
    // script allocates.
    if (SETTINGS.scriptStripDebug) luaStripDebugInfo(L_, -1);
    status = lua_pcall(L_, 0, 0, msgh);
  }

  bool ok = (status == LUA_OK);
  if (!ok) {
    const char* msg = lua_tostring(L_, -1);
    errorOut = msg ? msg : "unknown error";
    lua_pop(L_, 1);  // error object

    // An out-of-memory failure arrives with no traceback and no numbers:
    // luaM_error raises LUA_ERRMEM directly, bypassing the message handler
    // above. Say how much was in use against how much was allowed, so "not
    // enough memory" becomes actionable instead of merely true.
    if (status == LUA_ERRMEM) {
      // Report the inputs, not just the verdict. The budget is derived from the
      // free heap at launch minus a reserve, then clamped — so "allowed" alone
      // cannot tell you whether the device was short of heap or the policy band
      // was the limit, and those want opposite fixes.
      char detail[224];
      snprintf(detail, sizeof(detail),
               "\n\nUsed %u KB of %u KB allowed.\nFree heap at start %u KB, reserved %u KB, limit set by %s.",
               (unsigned)((memPeak_ + 1023) / 1024), (unsigned)(memLimit_ / 1024),
               (unsigned)(freeHeapAtStart_ / 1024), (unsigned)(heapReserve_ / 1024), clampName());
      errorOut += detail;
    }
  }
  LOG_INF("LUA", "%s: lua peak %u/%u bytes, free heap %u", path.c_str(),
          (unsigned)memPeak_, (unsigned)memLimit_, (unsigned)ESP.getFreeHeap());
  lua_pop(L_, 1);  // message handler
  return ok;
}
