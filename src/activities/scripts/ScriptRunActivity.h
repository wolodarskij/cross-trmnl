#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"

class ScriptEngine;

// Runs one Lua script full-screen. The script owns the display via its
// screen.* API while running; on completion this shows a footer (success),
// the error+traceback (failure), or a "Stopped" screen (Back-aborted).
class ScriptRunActivity final : public Activity {
 public:
  // Ctor + dtor are out-of-line so unique_ptr<ScriptEngine>'s deleter is emitted
  // in the .cpp, where ScriptEngine is a complete type.
  ScriptRunActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);
  ~ScriptRunActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return state_ == RUNNING; }
  void render(RenderLock&&) override;

 private:
  enum State { RUNNING, FINISHED };
  void runScript();
  // Two compact lines of memory figures, empty unless the setting is on. Built
  // once and drawn by whichever end-of-run branch applies, so success, error
  // and abort cannot drift apart in what they report.
  std::vector<std::string> memoryReportLines() const;
  // Draws those lines over an already-composed screen, top-aligned on its own
  // cleared strip, and returns the height consumed (0 when there is nothing to
  // report). The strip is needed because on success the script's own artwork is
  // still on screen underneath.
  int drawMemoryReport(int w) const;

  std::string path_;
  std::string scriptName_;
  State state_ = RUNNING;
  bool started_ = false;
  bool splashDrawn_ = false;  // once true, RUNNING renders are stale no-ops
  bool ok_ = false;
  bool aborted_ = false;
  bool wifiStarted_ = false;
  std::string error_;
  std::vector<std::string> console_;
  std::unique_ptr<ScriptEngine> engine_;
};
