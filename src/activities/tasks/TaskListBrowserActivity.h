#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Lists the *.md checklists in /tasks and opens the selected one in a
// TasksActivity. Back returns to the home menu.
//
// Rendered pages (<name>-<page>.bmp) live in the same folder; the scan filters
// on .md so they never appear as lists.
class TaskListBrowserActivity final : public UiListActivity {
 public:
  explicit TaskListBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("TaskListBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(lists_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  void drawFooter() override;

  void loadLists();

  // Stem (no extension) of each list, which is also its id for page paths.
  std::vector<std::string> lists_;
  std::vector<freeink::ui::ListItem> rowItems_;
};
