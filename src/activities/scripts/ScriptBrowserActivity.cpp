#include "ScriptBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include "MappedInputManager.h"
#include "activities/scripts/ScriptRunActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kScriptsDir = "/scripts";
}

void ScriptBrowserActivity::loadScripts() {
  scripts_.clear();

  if (!Storage.exists(kScriptsDir)) Storage.mkdir(kScriptsDir);

  auto dir = Storage.open(kScriptsDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  char name[256];
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) {
      f.getName(name, sizeof(name));
      std::string_view fn{name};
      if (name[0] != '.' && FsHelpers::checkFileExtension(fn, ".lua")) {
        scripts_.emplace_back(name);
      }
    }
    f.close();
  }
  dir.close();
  FsHelpers::sortFileList(scripts_);
}

void ScriptBrowserActivity::onEnter() {
  Activity::onEnter();
  sel_ = 0;
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  loadScripts();
  requestUpdate();
}

void ScriptBrowserActivity::loop() {
  const int count = static_cast<int>(scripts_.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (count == 0) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    sel_ = (sel_ - 1 + count) % count;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    sel_ = (sel_ + 1) % count;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return;
    }
    const std::string path = std::string(kScriptsDir) + "/" + scripts_[sel_];
    auto run = makeUniqueNoThrow<ScriptRunActivity>(renderer, mappedInput, path);
    if (run) {
      startActivityForResult(std::move(run), [this](const ActivityResult&) { requestUpdate(); });
    }
  }
}

void ScriptBrowserActivity::render(RenderLock&&) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  renderer.clearScreen();

  Rect header;
  header.x = 0;
  header.y = 8;
  header.width = w;
  header.height = 40;
  GUI.drawHeader(renderer, header, tr(STR_SCRIPTS));

  if (scripts_.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 12, tr(STR_NO_SCRIPTS));
    renderer.drawCenteredText(SMALL_FONT_ID, h / 2 + 14, tr(STR_SCRIPTS_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int top = 60;
  const int footerH = 34;
  const int perPage = std::max(1, (h - top - footerH) / lineH);

  int start = 0;
  if (sel_ >= perPage) start = sel_ - perPage + 1;

  for (int i = 0; i < perPage && start + i < static_cast<int>(scripts_.size()); i++) {
    const int idx = start + i;
    const int y = top + i * lineH;
    const bool selected = (idx == sel_);
    if (selected) renderer.fillRect(4, y - 3, w - 8, lineH, true);
    renderer.drawText(UI_10_FONT_ID, 14, y, scripts_[idx].c_str(), !selected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
