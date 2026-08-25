#include "ScriptRunActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
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
  splashDrawn_ = false;
  // No requestUpdate() here: loop() paints the splash synchronously via
  // requestUpdateAndWait(). A queued async render would race the script's
  // first frames and paint "Running…" over them.
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

std::vector<std::string> ScriptRunActivity::memoryReportLines() const {
  if (!SETTINGS.scriptMemReport || !engine_) return {};
  // Peak against limit answers "did this nearly not fit"; the second line
  // answers "and what set that limit", which is the part you can act on — the
  // reserve is a setting, the clamp bounds are not.
  char line1[80];
  snprintf(line1, sizeof(line1), "Lua peak %u KB of %u KB allowed", (unsigned)((engine_->memPeak() + 1023) / 1024),
           (unsigned)(engine_->memLimit() / 1024));
  char line2[96];
  snprintf(line2, sizeof(line2), "Free heap %u KB - reserve %u KB, limit by %s",
           (unsigned)(engine_->freeHeapAtStart() / 1024), (unsigned)(engine_->heapReserve() / 1024),
           engine_->clampName());
  return {line1, line2};
}

int ScriptRunActivity::drawMemoryReport(int w) const {
  const auto lines = memoryReportLines();
  if (lines.empty()) return 0;

  const int lh = renderer.getLineHeight(SMALL_FONT_ID) + 2;
  const int height = lh * static_cast<int>(lines.size()) + 10;
  // A successful script still owns the screen underneath, so the strip has to
  // be cleared before writing into it or the figures land on top of artwork.
  renderer.fillRect(0, 0, w, height, false);
  renderer.drawLine(0, height - 1, w, height - 1);
  int y = 4;
  for (const auto& line : lines) {
    renderer.drawText(SMALL_FONT_ID, 8, y, line.c_str());
    y += lh;
  }
  return height;
}

void ScriptRunActivity::loop() {
  if (state_ == RUNNING) {
    if (!started_) {
      started_ = true;
      requestUpdateAndWait();  // ensure the "Running…" screen is shown first
      splashDrawn_ = true;     // from here on the script owns the screen
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
    // A stale queued render must not repaint the splash over the running
    // script's frames — the script draws to the screen directly.
    if (splashDrawn_) return;
    renderer.clearScreen();
    std::string msg = std::string(tr(STR_SCRIPT_RUNNING)) + " " + scriptName_;
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 8, msg.c_str());
    renderer.displayBuffer();
    return;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");

  if (ok_) {
    // Success: keep whatever the script drew, just overlay the exit hint.
    drawMemoryReport(w);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  renderer.clearScreen();
  // Reported on every end-of-run path, not just success: the run that ran out
  // of memory is the one whose figures matter most.
  const int memH = drawMemoryReport(w);
  if (aborted_) {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 8, tr(STR_SCRIPT_STOPPED), true, EpdFontFamily::BOLD);
  } else {
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, memH + metrics.topPadding, w, metrics.headerHeight}, tr(STR_SCRIPT_ERROR));
    const auto lines = renderer.wrappedText(SMALL_FONT_ID, error_.c_str(), w - 24, 12);
    int y = memH + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int lh = renderer.getLineHeight(SMALL_FONT_ID) + 2;
    for (const auto& line : lines) {
      renderer.drawText(SMALL_FONT_ID, 12, y, line.c_str());
      y += lh;
    }
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
