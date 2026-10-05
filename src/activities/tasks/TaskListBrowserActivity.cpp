#include "TaskListBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/editor/TextEditorActivity.h"
#include "activities/tasks/TasksActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/editor.h"
#include "tasks/TaskFile.h"

namespace fui = freeink::ui;

namespace {
// Long enough for a real list name, short enough to stay one header line.
constexpr size_t kMaxListNameChars = 48;
}  // namespace

void TaskListBrowserActivity::loadLists() {
  rowItems_.clear();
  tasks::scanLists(lists_);

  rowItems_.reserve(lists_.size() + 1);
  for (size_t i = 0; i < lists_.size(); i++) {
    fui::ListItem item;
    item.label = lists_[i].c_str();
    item.icon = listIconFor(UIIcon::Text);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }

  // Always last, so row i stays lists_[i] for every list row.
  fui::ListItem newRow;
  newRow.label = tr(STR_TASK_NEW_LIST);
  newRow.icon = fui::bitmapFromIcon(icon_file_plus_24);
  newRow.actionValue = static_cast<int16_t>(lists_.size());
  rowItems_.push_back(newRow);
}

void TaskListBrowserActivity::reloadAndSelect(const std::string& listId) {
  loadLists();
  for (size_t i = 0; i < lists_.size(); i++) {
    if (lists_[i] == listId) {
      moveSelectionTo(static_cast<int>(i));
      return;
    }
  }
  const int last = static_cast<int>(rowItems_.size()) - 1;
  if (nav.selected > last) moveSelectionTo(last);
}

void TaskListBrowserActivity::onEnter() {
  UiListActivity::onEnter();
  // Reopening the browser lands on the list that was last used, which is the
  // one the sleep screen is showing.
  reloadAndSelect(APP_STATE.taskListId);
  requestUpdate();
}

const char* TaskListBrowserActivity::headerTitle() const { return tr(STR_TASKS); }

void TaskListBrowserActivity::onBackButton() { onGoHome(); }

void TaskListBrowserActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowItems_.size())) return;
  app.clearTapFlash();  // we are leaving this screen; a lingering flash would gray a row on return

  if (index == static_cast<int>(lists_.size())) {
    createList();
    return;
  }

  const std::string listId = lists_[index];
  auto view = makeUniqueNoThrow<TasksActivity>(renderer, mappedInput, listId);
  if (!view) {
    LOG_ERR("TSK", "OOM: task view for %s", listId.c_str());
    return;
  }
  APP_STATE.taskListId = listId;
  APP_STATE.saveToFile();
  // The view does not create or delete lists, but its editor can be used to
  // save the file under another name, so rescan on return.
  startActivityForResult(std::move(view), [this, listId](const ActivityResult&) {
    reloadAndSelect(listId);
    requestUpdate();
  });
}

void TaskListBrowserActivity::createList() {
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TASK_LIST_NAME), "", kMaxListNameChars);
  if (!keyboard) {
    LOG_ERR("TSK", "OOM: keyboard for new list");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    // Same FAT32 clean-up as the editor's "+ New text file". A typed ".md" is
    // dropped because the id is the stem and listPath() adds the extension.
    char sanitized[128];
    FsHelpers::sanitizePathComponentForFat32(std::get<KeyboardResult>(result.data).text.c_str(), sanitized,
                                             sizeof(sanitized));
    std::string listId{sanitized};
    if (FsHelpers::hasMarkdownExtension(listId)) listId.resize(listId.size() - 3);
    if (listId.empty()) {
      requestUpdate();
      return;
    }

    if (!tasks::createList(listId)) {
      GUI.drawPopup(renderer, tr(STR_EDITOR_SAVE_FAILED));
      renderer.displayBuffer();
      delay(900);
      reloadAndSelect(listId);
      requestUpdate();
      return;
    }

    auto editor = makeUniqueNoThrow<TextEditorActivity>(renderer, mappedInput, tasks::listPath(listId));
    if (!editor) {
      LOG_ERR("TSK", "OOM: editor for new list %s", listId.c_str());
      reloadAndSelect(listId);
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(editor), [this, listId](const ActivityResult&) {
      reloadAndSelect(listId);
      requestUpdate();
    });
  });
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
