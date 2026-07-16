#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// Lists *.lua files in /scripts and launches the selected one in a
// ScriptRunActivity. Back returns to the home menu.
class ScriptBrowserActivity final : public Activity {
 public:
  explicit ScriptBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScriptBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void loadScripts();

  std::vector<std::string> scripts_;
  int sel_ = 0;
  bool lockNextConfirmRelease = false;
};
