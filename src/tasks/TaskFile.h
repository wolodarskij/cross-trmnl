#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tasks {
// User-visible on purpose, alongside /scripts and /dashboards: dropping a
// Markdown checklist in here is a supported way to add a list. Rendered pages
// live beside the lists as <name>-<page>.bmp; the scan filters on .md, so they
// never show up as lists themselves.
inline constexpr const char* kTasksDir = "/tasks";

// Stems (no extension) of the .md lists in /tasks, sorted. Creates the folder
// when it is missing. Pure SD work, safe to call from onEnter().
void scanLists(std::vector<std::string>& out);

// "/tasks/<listId>.md"
std::string listPath(const std::string& listId);
}  // namespace tasks

// A Markdown checklist on the SD card.
//
// Format (ordinary Markdown, so the same file stays editable in the text
// editor — the two tools share the files rather than the code):
//
//   # Shipping
//   - [ ] Buy milk
//   - [/] Write the plan          due:2026-09-20
//   - [x] Ship firmware
//
// `[ ]` open, `[/]` in progress, `[x]` done; `[/]` is the convention Obsidian
// uses. A trailing `due:YYYY-MM-DD` is optional and is shown in the row's value
// column.
//
// Lossless by construction: every line keeps its original bytes in `raw`, and
// the only edit this class can make is overwriting the single character between
// the brackets. Lines the parser does not recognise — comments, prose, nested
// bullets, a `[!]` marker some other tool wrote — are carried through untouched.
// A save can therefore never damage a file this class did not fully understand.
//
// Bounds exist because the whole document lives in RAM: the line vector needs
// one contiguous block, and a task list is small by nature. Anything larger
// stays readable in the text editor and the normal reader.
enum class TaskState : uint8_t { Open = 0, InProgress = 1, Done = 2 };

struct TaskLine {
  std::string raw;  // verbatim, minus the line terminator

  // Spans into `raw`. Kept as offsets rather than copies so a 500-line list
  // costs one string per line instead of three.
  uint16_t textBegin = 0;
  uint16_t textEnd = 0;
  uint16_t dueBegin = 0;
  uint16_t dueEnd = 0;
  uint16_t markerPos = 0;  // index of the state character inside the brackets

  TaskState state = TaskState::Open;
  bool isTask = false;
  bool isHeading = false;
};

class TaskFile {
 public:
  enum class Error : uint8_t { None, OpenFailed, TooLarge, TooManyLines, NoHeap };

  // A task list is small by nature; these caps keep the in-RAM document bounded
  // and are what the "list too large" screen reports against.
  static constexpr size_t kMaxBytes = 16 * 1024;
  static constexpr size_t kMaxLines = 500;

  bool load(const std::string& path);
  // Crash-safe: temp file -> flush -> close -> remove + rename, with the temp
  // dropped on any failure. Same shape as TextEditorActivity::saveFile().
  bool save();

  bool setState(size_t index, TaskState state);
  // Open -> InProgress -> Done -> Open. No-op on a non-task line.
  bool cycleState(size_t index);

  const std::vector<TaskLine>& lines() const { return lines_; }
  size_t lineCount() const { return lines_.size(); }
  bool isDirty() const { return dirty_; }
  Error error() const { return error_; }

  // Copies of the spans, for callers that need NUL-terminated storage
  // (FreeInkUI list rows hold raw const char*).
  std::string textAt(size_t index) const;
  std::string dueAt(size_t index) const;

  size_t countWithState(TaskState state) const;
  size_t taskCount() const;

  // Loaded file size and line count, for the error screens.
  size_t loadedBytes() const { return loadedBytes_; }
  size_t loadedLines() const { return loadedLines_; }

 private:
  // Parses one line into a TaskLine.
  static TaskLine parseLine(std::string raw);

  std::vector<TaskLine> lines_;
  std::string path_;
  bool dirty_ = false;
  bool usesCrlf_ = false;
  bool hadTrailingNewline_ = true;
  Error error_ = Error::None;
  size_t loadedBytes_ = 0;
  size_t loadedLines_ = 0;
};
