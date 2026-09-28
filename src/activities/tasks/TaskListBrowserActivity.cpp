#include "TaskListBrowserActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/tasks/TasksActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "tasks/TaskFile.h"

namespace fui = freeink::ui;

void TaskListBrowserActivity::loadLists() {
  rowItems_.clear();
  tasks::scanLists(lists_);

  rowItems_.reserve(lists_.size());
  for (size_t i = 0; i < lists_.size(); i++) {
    fui::ListItem item;
    item.label = lists_[i].c_str();
    item.icon = listIconFor(UIIcon::Text);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

void TaskListBrowserActivity::onEnter() {
  UiListActivity::onEnter();
  loadLists();
  // Reopening the browser lands on the list that was last used, which is the
  // one the sleep screen is showing.
  if (!APP_STATE.taskListId.empty()) {
    for (size_t i = 0; i < lists_.size(); i++) {
      if (lists_[i] == APP_STATE.taskListId) {
        moveSelectionTo(static_cast<int>(i));
        break;
      }
    }
  }
  requestUpdate();
}

const char* TaskListBrowserActivity::headerTitle() const { return tr(STR_TASKS); }

void TaskListBrowserActivity::onBackButton() { onGoHome(); }

void TaskListBrowserActivity::drawFooter() {
  if (lists_.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  UiListActivity::drawFooter();
}

void TaskListBrowserActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(lists_.size())) return;
  app.clearTapFlash();  // we are leaving this screen; a lingering flash would gray a row on return

  const std::string listId = lists_[index];
  auto view = makeUniqueNoThrow<TasksActivity>(renderer, mappedInput, listId);
  if (!view) {
    LOG_ERR("TSK", "OOM: task view for %s", listId.c_str());
    return;
  }
  APP_STATE.taskListId = listId;
  APP_STATE.saveToFile();
  // The view cannot create or delete lists, so the rows stay valid; only the
  // marks and the rendered pages change behind it.
  startActivityForResult(std::move(view), [this](const ActivityResult&) { requestUpdate(); });
}

void TaskListBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void TaskListBrowserActivity::render(RenderLock&& lock) {
  if (!lists_.empty()) {
    UiListActivity::render(std::move(lock));
    return;
  }

  // Nothing to list: say where lists come from rather than showing a blank page.
  const int h = renderer.getScreenHeight();
  renderer.clearScreen();
  drawChrome();
  renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 12, tr(STR_NO_TASK_LISTS));
  renderer.drawCenteredText(SMALL_FONT_ID, h / 2 + 14, tr(STR_TASKS_HINT));
  drawFooter();
  renderer.displayBuffer();
}
