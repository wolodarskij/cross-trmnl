#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Lists the *.md checklists in /tasks and opens the selected one in a
// TasksActivity. A final "+ New list" row names a list on the keyboard, writes
// it, and opens it in TextEditorActivity. Back returns to the home menu.
//
// Rendered pages (<name>-<page>.bmp) live in the same folder; the scan filters
// on .md so they never appear as lists.
class TaskListBrowserActivity final : public UiListActivity {
 public:
  explicit TaskListBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("TaskListBrowser", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;

  void loadLists();
  // Re-scans /tasks and puts the selection on `listId` when it is listed.
  void reloadAndSelect(const std::string& listId);
  // Keyboard for the name -> createList() -> TextEditorActivity.
  void createList();

  // Stem (no extension) of each list, which is also its id for page paths.
  // Row i is lists_[i]; the "+ New list" row comes after them.
  std::vector<std::string> lists_;
  std::vector<freeink::ui::ListItem> rowItems_;
};
