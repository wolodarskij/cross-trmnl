#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct lua_State;
class GfxRenderer;
class MappedInputManager;
class ScriptEngine;

// Shared state the Lua host bindings need. A pointer to this lives in the Lua
// state's extra space so C closures can reach the device without globals.
struct ScriptContext {
  GfxRenderer* renderer = nullptr;
  MappedInputManager* input = nullptr;
  std::vector<std::string>* console = nullptr;  // print()/device.log() output
  ScriptEngine* engine = nullptr;               // owner; device.mem() reads its counters

  bool wifiStartedByScript = false;  // set when http.get brings WiFi up
  bool aborted = false;              // force-quit (long-hold Back / device.exit)

  // Cooperative abort/pump: the count hook and blocking bindings (input.wait,
  // device.sleep) call pump() so a runaway script stays interruptible.
  unsigned long lastPumpMs = 0;

  // Soft vs hard Back: short press → backSoftPending (delivered as "back");
  // hold ≥ kBackHoldExitMs → aborted (always exits, game cannot override).
  unsigned long backDownMs = 0;  // 0 = Back not held
  bool backSoftPending = false;
};

// Owns a sandboxed Lua 5.4 interpreter for running one script at a time.
// Safety rails: a bounded allocator (scripts can't exhaust the heap), a count
// hook that lets long-hold Back abort infinite loops, and pcall error+traceback
// capture so a bad script never crashes the firmware.
class ScriptEngine {
 public:
  ScriptEngine(GfxRenderer& renderer, MappedInputManager& input, std::vector<std::string>& console);
  ~ScriptEngine();

  // Creates the interpreter, opens the safe libs, and registers the device API.
  // Returns false on out-of-memory (nothing else is usable then).
  bool begin();

  // Loads and runs the script at `path`. On failure fills `errorOut` with the
  // Lua message + traceback and returns false.
  bool runFile(const std::string& path, std::string& errorOut);

  ScriptContext& context() { return ctx_; }
  bool wifiStartedByScript() const { return ctx_.wifiStartedByScript; }

  // Pumps input and returns true if the run should abort (long-hold Back).
  // Short Back releases set backSoftPending for input.wait/poll. Used by the
  // count hook and by blocking bindings.
  static bool pump(lua_State* L);

  // Hold threshold for force-exit (milliseconds).
  static constexpr unsigned long kBackHoldExitMs = 800;

  // Lua memory budget, in bytes. The cap is a POLICY limit, not the safety
  // mechanism: safety comes from alloc() returning nullptr, which Lua handles
  // by running an emergency full collection and retrying (lib/Lua/lmem.c), and
  // only then raising LUA_ERRMEM. So the ceiling can be raised without any risk
  // of crashing the firmware — it only decides how much of the heap a script is
  // allowed to claim before the reader's own allocations start to be at risk.
  //
  // kMemFloor was the old fixed cap. It proved too small for a dialog-driven
  // game: the shared engine alone costs ~62 KB on-device, and the sample
  // adventure needs ~114 KB, so it died part-way through loading its story.
  //
  // The budget is freeHeap - reserve, clamped to [kMemFloor, kMemCeiling]. The
  // reserve is CrossPointSettings::scriptHeapReserveKb rather than a constant:
  // how much the rest of the system needs depends on what the user actually
  // runs, and that is not knowable at compile time. These two bounds stay fixed
  // because they are the band the *policy* allows, not a measurement.
  static constexpr size_t kMemFloor = 96 * 1024;
  static constexpr size_t kMemCeiling = 160 * 1024;

  size_t memUsed() const { return memUsed_; }
  size_t memPeak() const { return memPeak_; }
  size_t memLimit() const { return memLimit_; }
  // Free heap as begin() saw it. Re-reading later answers a different question,
  // because by then the script's own arena is part of what is missing.
  size_t freeHeapAtStart() const { return freeHeapAtStart_; }
  // What was actually held back, after clamping whatever the setting said.
  size_t heapReserve() const { return heapReserve_; }
  // Which bound, if either, decided the budget rather than the free heap.
  const char* clampName() const {
    return memClamp_ == Clamp::Floor ? "floor" : (memClamp_ == Clamp::Ceiling ? "ceiling" : "none");
  }

 private:
  static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);

  enum class Clamp { None, Floor, Ceiling };

  lua_State* L_ = nullptr;
  ScriptContext ctx_;
  size_t memUsed_ = 0;
  size_t memPeak_ = 0;
  size_t memLimit_ = kMemFloor;  // replaced in begin() by a heap-derived budget
  size_t freeHeapAtStart_ = 0;
  size_t heapReserve_ = 0;  // read from settings in begin()
  Clamp memClamp_ = Clamp::None;
};
