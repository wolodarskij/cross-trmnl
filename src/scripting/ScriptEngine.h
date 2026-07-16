#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct lua_State;
class GfxRenderer;
class MappedInputManager;

// Shared state the Lua host bindings need. A pointer to this lives in the Lua
// state's extra space so C closures can reach the device without globals.
struct ScriptContext {
  GfxRenderer* renderer = nullptr;
  MappedInputManager* input = nullptr;
  std::vector<std::string>* console = nullptr;  // print()/device.log() output

  bool wifiStartedByScript = false;  // set when http.get brings WiFi up
  bool aborted = false;              // Back pressed during a run

  // Cooperative abort/pump: the count hook and blocking bindings (input.wait,
  // device.sleep) call pump() so a runaway script stays interruptible.
  unsigned long lastPumpMs = 0;
};

// Owns a sandboxed Lua 5.4 interpreter for running one script at a time.
// Safety rails: a bounded allocator (scripts can't exhaust the heap), a count
// hook that lets Back abort infinite loops, and pcall error+traceback capture
// so a bad script never crashes the firmware.
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

  // Pumps input and returns true if the run should abort (Back pressed).
  // Used by the count hook and by blocking bindings.
  static bool pump(lua_State* L);

 private:
  static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);

  lua_State* L_ = nullptr;
  ScriptContext ctx_;
  size_t memUsed_ = 0;
  size_t memLimit_ = 96 * 1024;  // hard cap so a script can't OOM the device
};
