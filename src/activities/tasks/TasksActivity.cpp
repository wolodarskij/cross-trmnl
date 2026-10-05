#include "TasksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/editor/TextEditorActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/taskStateIcons.h"
#include "fontIds.h"
#include "tasks/TaskRenderer.h"

namespace fui = freeink::ui;

namespace {

constexpr unsigned long kOptionsHoldMs = 700;

fui::BitmapRef stateIcon(const TaskState state) {
  switch (state) {
    case TaskState::InProgress:
      return fui::bitmapFromIcon(icon_task_progress_24);
    case TaskState::Done:
      return fui::bitmapFromIcon(icon_task_done_24);
    default:
      return fui::bitmapFromIcon(icon_task_open_24);
  }
}

StrId stateLabel(const TaskState state) {
  switch (state) {
    case TaskState::InProgress:
      return StrId::STR_TASK_IN_PROGRESS;
    case TaskState::Done:
      return StrId::STR_TASK_DONE;
    default:
      return StrId::STR_TASK_OPEN;
  }
}

StrId errorLabel(const TaskFile::Error error) {
  switch (error) {
    case TaskFile::Error::TooLarge:
    case TaskFile::Error::TooManyLines:
      return StrId::STR_TASK_TOO_LARGE;
    case TaskFile::Error::NoHeap:
      return StrId::STR_TASK_NO_HEAP;
    default:
      return StrId::STR_TASK_LOAD_FAILED;
  }
}

}  // namespace

void TasksActivity::onEnter() {
  UiListActivity::onEnter();
  hideDone_ = SETTINGS.taskHideDone != 0;
  file_.load(tasks::listPath(listId_));
  rebuildRows();
  requestUpdate();
}

void TasksActivity::onExit() {
  // The only save that always runs: the device can sleep from here without
  // loop() getting a say, and ActivityManager runs onExit() on the way down.
  // Not while the editor is on top: file_ is unloaded then, the editor's own
  // onExit() has already saved, and the pages catch up on the next render
  // (the sleep screen and this screen both check the content stamp).
  if (!editing_) saveAndRenderPages();
  Activity::onExit();
}

void TasksActivity::rebuildRows() {
  rowLines_.clear();
  rowLabels_.clear();
  rowValues_.clear();
  rowItems_.clear();

  const size_t total = file_.lineCount();
  rowLines_.reserve(total);
  rowLabels_.reserve(total);
  rowValues_.reserve(total);

  for (size_t i = 0; i < total; i++) {
    const TaskLine& line = file_.lines()[i];
    const bool isHeading = line.isHeading;
    if (!isHeading && !line.isTask) continue;
    if (!isHeading && hideDone_ && line.state == TaskState::Done) continue;

    rowLines_.push_back(i);
    rowLabels_.push_back(file_.textAt(i));
    rowValues_.push_back(isHeading ? std::string() : file_.dueAt(i));
  }

  // Pointers are taken only after every push_back is done: growing either vector
  // would leave the rows pointing at freed storage.
  rowItems_.reserve(rowLines_.size());
  for (size_t r = 0; r < rowLines_.size(); r++) {
    const TaskLine& line = file_.lines()[rowLines_[r]];
    fui::ListItem item;
    item.label = rowLabels_[r].c_str();
    item.actionValue = static_cast<int16_t>(r);
    if (line.isHeading) {
      item.isHeader = true;
    } else {
      item.icon = stateIcon(line.state);
      // The due date when there is one, the state in words when there is not:
      // either way the row says more than the checkbox alone.
      item.value = rowValues_[r].empty() ? I18N.get(stateLabel(line.state)) : rowValues_[r].c_str();
    }
    rowItems_.push_back(item);
  }
}

void TasksActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowLines_.size())) return;
  if (optionPopup.isActive()) return;

  const size_t lineIndex = rowLines_[index];
  if (file_.lines()[lineIndex].isHeading) return;

  nav.selected = index;
  // The row stays on screen with a new glyph; a lingering flash would gray an
  // unrelated row on the repaint below.
  app.clearTapFlash();

  if (!file_.cycleState(lineIndex)) return;

  if (hideDone_ && file_.lines()[lineIndex].state == TaskState::Done) {
    // The row just filtered itself away; rebuild and keep the selection in range
    // so the next press lands somewhere sensible.
    rebuildRows();
    const int last = static_cast<int>(rowItems_.size()) - 1;
    moveSelectionTo(index > last ? (last < 0 ? 0 : last) : index);
  } else {
    const TaskLine& line = file_.lines()[lineIndex];
    rowValues_[index] = file_.dueAt(lineIndex);
    rowItems_[index].icon = stateIcon(line.state);
    rowItems_[index].value = rowValues_[index].empty() ? I18N.get(stateLabel(line.state)) : rowValues_[index].c_str();
  }
  requestUpdate();
}

void TasksActivity::onRowLongPress(const int index) {
  if (index >= 0 && index < static_cast<int>(rowItems_.size())) nav.selected = index;
  openOptions();
}

void TasksActivity::openOptions() {
  app.clearTapFlash();
  const std::vector<std::string> options = {
      I18N.get(StrId::STR_TASK_EDIT),
      I18N.get(hideDone_ ? StrId::STR_TASK_SHOW_DONE : StrId::STR_TASK_HIDE_DONE),
      I18N.get(StrId::STR_TASK_EXPORT),
  };
  optionPopup.show(StrId::STR_TASK_OPTIONS, options, 0, [this](const int selected) {
    if (selected == 0) {
      openEditor();
    } else if (selected == 1) {
      hideDone_ = !hideDone_;
      // Persisted immediately: relying on a parent's result callback loses the
      // change when the screen is left via Home or sleep.
      SETTINGS.taskHideDone = hideDone_ ? 1 : 0;
      SETTINGS.saveToFile();
      rebuildRows();
      const int last = static_cast<int>(rowItems_.size()) - 1;
      moveSelectionTo(nav.selected > last ? (last < 0 ? 0 : last) : nav.selected.load());
    } else if (selected == 2) {
      showMessage(saveAndRenderPages() ? StrId::STR_TASK_EXPORTED : StrId::STR_TASK_EXPORT_FAILED);
    }
  });
  requestUpdate();
}

void TasksActivity::openEditor() {
  if (editing_) return;
  app.clearTapFlash();

  // The editor reads the file from the card, so pending marks go there first.
  if (file_.error() == TaskFile::Error::None && file_.isDirty() && !file_.save()) {
    LOG_ERR("TSK", "Failed to save %s before editing", listId_.c_str());
    showMessage(StrId::STR_EDITOR_SAVE_FAILED);
    return;
  }

  auto editor = makeUniqueNoThrow<TextEditorActivity>(renderer, mappedInput, tasks::listPath(listId_));
  if (!editor) {
    LOG_ERR("TSK", "OOM: editor for %s", listId_.c_str());
    return;
  }

  // Both documents are whole-file in RAM (16 KB list, 32 KB editor cap), so the
  // list and its row strings are released before the editor loads its copy.
  file_.unload();
  std::vector<size_t>().swap(rowLines_);
  std::vector<std::string>().swap(rowLabels_);
  std::vector<std::string>().swap(rowValues_);
  std::vector<freeink::ui::ListItem>().swap(rowItems_);
  editing_ = true;

  startActivityForResult(std::move(editor), [this](const ActivityResult&) {
    editing_ = false;
    file_.load(tasks::listPath(listId_));
    rebuildRows();
    const int last = static_cast<int>(rowItems_.size()) - 1;
    moveSelectionTo(nav.selected > last ? (last < 0 ? 0 : last) : nav.selected.load());
    requestUpdate();
  });
}

bool TasksActivity::saveAndRenderPages() {
  if (file_.error() != TaskFile::Error::None) return true;  // nothing was ever loaded

  if (file_.isDirty() && !file_.save()) {
    LOG_ERR("TSK", "Failed to save %s", listId_.c_str());
    return false;
  }

  const int fontId = taskrender::resolveFontId(renderer);
  if (!taskrender::needsRender(file_, listId_, fontId, hideDone_)) return true;

  // Rendering owns the framebuffer for the duration — it is the only one there
  // is — so put the screen back afterwards.
  const bool stored = renderer.storeBwBuffer();
  const size_t pages = taskrender::renderToBmps(renderer, file_, listId_, fontId, hideDone_);
  if (stored) renderer.restoreBwBuffer();
  return pages > 0;
}

void TasksActivity::showMessage(const StrId id) {
  GUI.drawPopup(renderer, I18N.get(id));
  renderer.displayBuffer();
  delay(900);
  requestUpdate();
}

bool TasksActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool TasksActivity::handleButtons() {
  // Hold Confirm for the options, so button-only boards reach them without the
  // touch long-press. wasLongPressed() suppresses the release that follows.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kOptionsHoldMs)) {
    openOptions();
    return true;
  }
  // Nothing to mark (an empty list, or one that would not load): Confirm goes
  // straight to the editor, which is where such a list gets fixed.
  if ((file_.error() != TaskFile::Error::None || rowItems_.empty()) &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openEditor();
    return true;
  }
  return UiListActivity::handleButtons();
}

void TasksActivity::drawFooter() {
  if (file_.error() != TaskFile::Error::None || rowItems_.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TASK_EDIT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  UiListActivity::drawFooter();
}

void TasksActivity::buildScreen(UiScreen& screen) {
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
  props.inputMask = static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress);  // buttons stay in loop()
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void TasksActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  if (file_.error() == TaskFile::Error::None && !rowItems_.empty()) {
    UiListActivity::render(std::move(lock));
    return;
  }

  // Load failure, or a file with no tasks in it: say which, rather than
  // showing an empty list the buttons do nothing in.
  const int h = renderer.getScreenHeight();
  renderer.clearScreen();
  drawChrome();
  if (file_.error() != TaskFile::Error::None) {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 12, I18N.get(errorLabel(file_.error())));
    if (file_.error() == TaskFile::Error::TooLarge || file_.error() == TaskFile::Error::TooManyLines) {
      char detail[64];
      snprintf(detail, sizeof(detail), "%u bytes, %u lines", (unsigned)file_.loadedBytes(),
               (unsigned)file_.loadedLines());
      renderer.drawCenteredText(SMALL_FONT_ID, h / 2 + 14, detail);
    }
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 12, tr(STR_TASK_LIST_EMPTY));
    if (hideDone_ && file_.taskCount() > 0) {
      renderer.drawCenteredText(SMALL_FONT_ID, h / 2 + 14, tr(STR_TASK_SHOW_DONE));
    }
  }
  renderer.drawCenteredText(SMALL_FONT_ID, h / 2 + 40, tr(STR_TASK_EDIT_HINT));
  drawFooter();
  renderer.displayBuffer();
}
