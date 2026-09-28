#pragma once

#include <I18n.h>

#include <cstddef>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "tasks/TaskFile.h"

// Views one Markdown checklist from /tasks and lets its tasks be marked.
//
// Deliberately not an editor: Confirm cycles the selected task through
// open -> in progress -> done and that is the whole of it. Writing task text
// stays in TextEditorActivity, which already edits .md files — the two tools
// share the files rather than duplicating each other's code.
//
// Leaving the screen writes the list back (when anything changed) and re-renders
// its BMP pages, so the Tasks sleep screen is current without the user having to
// think about it. onExit() runs on stacked activities during the sleep
// transition, which is what makes "on exit" and "before sleep" the same path.
class TasksActivity final : public UiListActivity {
 public:
  TasksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string listId)
      : UiListActivity("Tasks", renderer, mappedInput, /*wantsTouchLongPress=*/true), listId_(std::move(listId)) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override { return listId_.c_str(); }
  void drawFooter() override;

  // Structural rebuild of the row arrays. Called on load and whenever the
  // hide-done filter changes — never from buildScreen(), which runs per repaint.
  void rebuildRows();
  void openOptions();
  // Writes the list back if dirty, then re-renders the pages if anything the
  // pages depend on changed. Returns false only when something actually failed.
  bool saveAndRenderPages();
  void showMessage(StrId id);

  std::string listId_;
  TaskFile file_;
  bool hideDone_ = false;

  // FreeInkUI rows hold raw const char*, so the backing strings have to outlive
  // every build. Parallel arrays, all indexed by row.
  std::vector<size_t> rowLines_;  // row -> index into file_.lines()
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;

  OptionPopup optionPopup;
};
