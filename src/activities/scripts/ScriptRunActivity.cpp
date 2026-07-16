#include "ScriptRunActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "scripting/ScriptEngine.h"

ScriptRunActivity::ScriptRunActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("ScriptRun", renderer, mappedInput), path_(std::move(path)) {}

ScriptRunActivity::~ScriptRunActivity() = default;

void ScriptRunActivity::onEnter() {
  Activity::onEnter();
  const auto slash = path_.find_last_of('/');
  scriptName_ = slash == std::string::npos ? path_ : path_.substr(slash + 1);
  state_ = RUNNING;
  started_ = false;
  requestUpdate();  // paint the "Running…" screen before the (blocking) run
}

void ScriptRunActivity::onExit() {
  Activity::onExit();
  engine_.reset();  // closes the Lua state
  // If the script brought WiFi up, tear it down and clear the heap
  // fragmentation from the TLS stack (same convention as DashboardActivity).
  if (wifiStarted_ && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void ScriptRunActivity::runScript() {
  engine_ = makeUniqueNoThrow<ScriptEngine>(renderer, mappedInput, console_);
  if (!engine_ || !engine_->begin()) {
    ok_ = false;
    error_ = "out of memory starting Lua";
    return;
  }
  ok_ = engine_->runFile(path_, error_);
  aborted_ = engine_->context().aborted;
  wifiStarted_ = engine_->wifiStartedByScript();
}

void ScriptRunActivity::loop() {
  if (state_ == RUNNING) {
    if (!started_) {
      started_ = true;
      requestUpdateAndWait();  // ensure the "Running…" screen is shown first
      runScript();
      state_ = FINISHED;
      requestUpdate();
    }
    return;
  }

  // FINISHED: any of Back/Confirm returns to the script list.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void ScriptRunActivity::render(RenderLock&&) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  if (state_ == RUNNING) {
    renderer.clearScreen();
    std::string msg = std::string(tr(STR_SCRIPT_RUNNING)) + " " + scriptName_;
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 8, msg.c_str());
    renderer.displayBuffer();
    return;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");

  if (ok_) {
    // Success: keep whatever the script drew, just overlay the exit hint.
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  renderer.clearScreen();
  if (aborted_) {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 8, tr(STR_SCRIPT_STOPPED), true, EpdFontFamily::BOLD);
  } else {
    Rect header;
    header.x = 0;
    header.y = 8;
    header.width = w;
    header.height = 32;
    GUI.drawHeader(renderer, header, tr(STR_SCRIPT_ERROR));
    const auto lines = renderer.wrappedText(SMALL_FONT_ID, error_.c_str(), w - 24, 12);
    int y = 56;
    const int lh = renderer.getLineHeight(SMALL_FONT_ID) + 2;
    for (const auto& line : lines) {
      renderer.drawText(SMALL_FONT_ID, 12, y, line.c_str());
      y += lh;
    }
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
