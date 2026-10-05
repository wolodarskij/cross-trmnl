#include "TaskFile.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string_view>

namespace {

constexpr size_t kLoadChunkBytes = 256;

bool isSpace(const char c) { return c == ' ' || c == '\t'; }

bool isDigits(const char* p, const size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
  }
  return true;
}

// "due:YYYY-MM-DD" — validated rather than merely prefix-matched, so a word
// like "due:soon" stays part of the task text instead of becoming a bad date.
bool isDueToken(const char* p, const size_t avail) {
  constexpr size_t kTokenLen = 4 + 10;  // "due:" + the date
  if (avail < kTokenLen) return false;
  if (memcmp(p, "due:", 4) != 0) return false;
  const char* d = p + 4;
  return isDigits(d, 4) && d[4] == '-' && isDigits(d + 5, 2) && d[7] == '-' && isDigits(d + 8, 2);
}

// Seeded when /tasks holds no lists, so the menu opens on something usable and
// the file doubles as a format reference. Lives in flash (.rodata); writing it
// needs no heap.
constexpr const char* kStarterListId = "todo";
constexpr char kStarterList[] =
    "# To do\n"
    "- [x] Open this list\n"
    "- [/] Press Confirm to change a task's state\n"
    "- [ ] Hold Confirm, then Edit list, to change the text\n"
    "- [ ] Give a task a due date          due:2026-12-31\n"
    "\n"
    "Lines that are not tasks, like this one, are kept as they are.\n";

// Writes a new list file. A partial file is removed so a failed write cannot
// leave a truncated list behind.
bool writeNewList(const std::string& path, const char* data, const size_t len) {
  bool ok = false;
  {
    HalFile f;
    if (!Storage.openFileForWrite("TSK", path, f)) {
      LOG_ERR("TSK", "Could not create list %s", path.c_str());
      return false;
    }
    ok = f.write(data, len) == len;
    f.flush();
  }
  if (!ok) {
    LOG_ERR("TSK", "Short write on list %s", path.c_str());
    Storage.remove(path.c_str());
    return false;
  }
  LOG_INF("TSK", "Created list %s", path.c_str());
  return true;
}

bool writeStarterList() {
  return writeNewList(tasks::listPath(kStarterListId), kStarterList, sizeof(kStarterList) - 1);
}

char markerFor(const TaskState state) {
  switch (state) {
    case TaskState::Done:
      return 'x';
    case TaskState::InProgress:
      return '/';
    default:
      return ' ';
  }
}

}  // namespace

namespace tasks {

void ensureDir() {
  if (Storage.exists(kTasksDir)) return;
  if (!Storage.mkdir(kTasksDir)) {
    LOG_ERR("TSK", "Could not create %s", kTasksDir);
    return;
  }
  writeStarterList();
}

void scanLists(std::vector<std::string>& out) {
  out.clear();
  ensureDir();

  auto dir = Storage.open(kTasksDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  char name[256];
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) {
      f.getName(name, sizeof(name));
      const std::string_view fn{name};
      // Only .md. The rendered <name>-<page>.bmp pages share this folder and
      // must never be offered as lists.
      if (name[0] != '.' && FsHelpers::checkFileExtension(fn, ".md")) {
        out.emplace_back(name, fn.size() - 3);  // the stem is the list id
      }
    }
    f.close();
  }
  dir.close();

  // No lists yet (a fresh card, or a folder emptied by hand): seed one rather
  // than leave the menu blank. The only list then is the starter, so there is
  // nothing to sort.
  if (out.empty()) {
    if (writeStarterList()) out.emplace_back(kStarterListId);
    return;
  }
  FsHelpers::sortFileList(out);
}

std::string listPath(const std::string& listId) { return std::string(kTasksDir) + "/" + listId + ".md"; }

bool createList(const std::string& listId) {
  const std::string path = listPath(listId);
  if (Storage.exists(path.c_str())) return true;
  ensureDir();
  // A heading plus one empty task, so the editor opens on a line that is
  // already a checkbox and only needs its text typed after it.
  const std::string body = "# " + listId + "\n- [ ] \n";
  return writeNewList(path, body.data(), body.size());
}

}  // namespace tasks

TaskLine TaskFile::parseLine(std::string raw) {
  TaskLine out;
  out.raw = std::move(raw);
  const std::string& s = out.raw;
  const size_t n = s.size();

  size_t i = 0;
  while (i < n && isSpace(s[i])) i++;

  // Heading: one or more '#' followed by a space.
  if (i < n && s[i] == '#') {
    size_t h = i;
    while (h < n && s[h] == '#') h++;
    if (h < n && s[h] == ' ') {
      out.isHeading = true;
      size_t t = h;
      while (t < n && isSpace(s[t])) t++;
      out.textBegin = static_cast<uint16_t>(t);
      out.textEnd = static_cast<uint16_t>(n);
      return out;
    }
    return out;
  }

  // Task: a bullet, a space, then "[<state>]" and a space.
  if (i >= n || (s[i] != '-' && s[i] != '*' && s[i] != '+')) return out;
  if (i + 4 >= n) return out;
  if (s[i + 1] != ' ' || s[i + 2] != '[' || s[i + 4] != ']') return out;

  // Only the three markers this tool owns make a line a task. Anything else
  // (`[!]`, `[?]`, whatever another tool wrote) stays an ordinary line, so
  // cycling a state can never overwrite a marker we do not understand.
  const char marker = s[i + 3];
  if (marker == ' ') {
    out.state = TaskState::Open;
  } else if (marker == 'x' || marker == 'X') {
    out.state = TaskState::Done;
  } else if (marker == '/') {
    out.state = TaskState::InProgress;
  } else {
    return out;
  }

  // A task needs either a space after the bracket or nothing at all after it.
  size_t t = i + 5;
  if (t < n && !isSpace(s[t])) return out;
  while (t < n && isSpace(s[t])) t++;

  out.isTask = true;
  out.markerPos = static_cast<uint16_t>(i + 3);

  // Pull a trailing due: token out of the displayed text.
  size_t end = n;
  for (size_t j = t; j < n; j++) {
    if (!isDueToken(s.c_str() + j, n - j)) continue;
    if (j > t && !isSpace(s[j - 1])) continue;  // must be its own word
    out.dueBegin = static_cast<uint16_t>(j + 4);
    out.dueEnd = static_cast<uint16_t>(j + 4 + 10);
    end = j;
    break;
  }
  while (end > t && isSpace(s[end - 1])) end--;

  out.textBegin = static_cast<uint16_t>(t);
  out.textEnd = static_cast<uint16_t>(end);
  return out;
}

bool TaskFile::load(const std::string& path) {
  lines_.clear();
  path_ = path;
  dirty_ = false;
  error_ = Error::None;
  loadedBytes_ = 0;
  loadedLines_ = 0;
  usesCrlf_ = false;
  hadTrailingNewline_ = true;

  HalFile f;
  if (!Storage.openFileForRead("TSK", path_, f)) {
    error_ = Error::OpenFailed;
    return false;
  }

  const size_t fileSize = f.fileSize();
  loadedBytes_ = fileSize;
  if (fileSize > kMaxBytes) {
    error_ = Error::TooLarge;
    return false;
  }

  uint8_t buf[kLoadChunkBytes];

  // Pass 1: count lines. The line vector needs lineCount * sizeof(TaskLine) as
  // one contiguous block, so it has to be known and gated before we reserve;
  // growing it instead would need old+new live at once and, under
  // -fno-exceptions, abort rather than fail.
  size_t lineCount = fileSize == 0 ? 0 : 1;
  size_t scanned = 0;
  while (scanned < fileSize) {
    const int got = f.read(buf, kLoadChunkBytes);
    if (got <= 0) break;
    for (int i = 0; i < got; i++) {
      if (buf[i] == '\n') lineCount++;
    }
    scanned += static_cast<size_t>(got);
  }
  loadedLines_ = lineCount;
  if (lineCount > kMaxLines) {
    error_ = Error::TooManyLines;
    return false;
  }

  const size_t vectorBytes = lineCount * sizeof(TaskLine);
  if (ESP.getFreeHeap() < vectorBytes + 2 * fileSize + 24 * 1024 || ESP.getMaxAllocHeap() < vectorBytes + 4 * 1024) {
    LOG_ERR("TSK", "%s: not enough heap (free=%u maxAlloc=%u need vec=%u size=%u)", path_.c_str(),
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)vectorBytes, (unsigned)fileSize);
    error_ = Error::NoHeap;
    return false;
  }

  if (!f.seekSet(0)) {
    error_ = Error::OpenFailed;
    return false;
  }

  // Pass 2: build the lines. Exact reserve, so no growth step ever runs.
  lines_.reserve(lineCount);
  std::string current;
  size_t readTotal = 0;
  while (readTotal < fileSize) {
    const int got = f.read(buf, kLoadChunkBytes);
    if (got <= 0) break;
    int segStart = 0;
    for (int i = 0; i < got; i++) {
      if (buf[i] != '\n') continue;
      current.append(reinterpret_cast<const char*>(buf + segStart), static_cast<size_t>(i - segStart));
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
        usesCrlf_ = true;
      }
      lines_.push_back(parseLine(std::move(current)));
      current.clear();
      segStart = i + 1;
    }
    current.append(reinterpret_cast<const char*>(buf + segStart), static_cast<size_t>(got - segStart));
    readTotal += static_cast<size_t>(got);
  }

  if (readTotal != fileSize) {
    LOG_ERR("TSK", "%s: short read %u of %u bytes", path_.c_str(), (unsigned)readTotal, (unsigned)fileSize);
    error_ = Error::OpenFailed;
    lines_.clear();
    return false;
  }

  // A file ending in a newline leaves `current` empty; anything left over is a
  // final line with no terminator.
  hadTrailingNewline_ = current.empty() && !lines_.empty();
  if (!current.empty()) lines_.push_back(parseLine(std::move(current)));
  return true;
}

void TaskFile::unload() {
  // clear() keeps the capacity; swapping with an empty vector releases it.
  std::vector<TaskLine>().swap(lines_);
  dirty_ = false;
  error_ = Error::None;
  loadedBytes_ = 0;
  loadedLines_ = 0;
}

bool TaskFile::save() {
  if (path_.empty()) return false;

  // ProgressFile::writeAtomic shape, as the text editor uses: an interrupted
  // save damages only the throwaway temp file.
  const std::string tmpPath = path_ + ".tmp";
  bool wrote = true;
  {
    HalFile f;
    if (!Storage.openFileForWrite("TSK", tmpPath, f)) {
      LOG_ERR("TSK", "Could not open temp file for write: %s", tmpPath.c_str());
      return false;
    }
    const char* eol = usesCrlf_ ? "\r\n" : "\n";
    const size_t eolLen = usesCrlf_ ? 2 : 1;
    for (size_t i = 0; i < lines_.size() && wrote; i++) {
      const std::string& l = lines_[i].raw;
      if (!l.empty() && f.write(l.data(), l.size()) != l.size()) {
        wrote = false;
        break;
      }
      const bool needsEol = i + 1 < lines_.size() || hadTrailingNewline_;
      if (needsEol && f.write(eol, eolLen) != eolLen) {
        wrote = false;
        break;
      }
    }
    f.flush();
    // f closes at scope exit (DESTRUCTOR_CLOSES_FILE); SdFat cannot rename a
    // path that still has an open handle.
  }

  if (!wrote) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  Storage.remove(path_.c_str());  // SdFat rename will not overwrite
  if (!Storage.rename(tmpPath.c_str(), path_.c_str())) {
    LOG_ERR("TSK", "Failed to rename %s into place", path_.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  dirty_ = false;
  return true;
}

bool TaskFile::setState(const size_t index, const TaskState state) {
  if (index >= lines_.size()) return false;
  TaskLine& line = lines_[index];
  if (!line.isTask) return false;
  if (line.state == state) return true;
  if (line.markerPos >= line.raw.size()) return false;

  // The only mutation this class makes: one character, in place. Everything
  // else about the line keeps its original bytes.
  line.raw[line.markerPos] = markerFor(state);
  line.state = state;
  dirty_ = true;
  return true;
}

bool TaskFile::cycleState(const size_t index) {
  if (index >= lines_.size() || !lines_[index].isTask) return false;
  switch (lines_[index].state) {
    case TaskState::Open:
      return setState(index, TaskState::InProgress);
    case TaskState::InProgress:
      return setState(index, TaskState::Done);
    default:
      return setState(index, TaskState::Open);
  }
}

std::string TaskFile::textAt(const size_t index) const {
  if (index >= lines_.size()) return {};
  const TaskLine& l = lines_[index];
  if (l.textEnd <= l.textBegin) return {};
  return l.raw.substr(l.textBegin, static_cast<size_t>(l.textEnd - l.textBegin));
}

std::string TaskFile::dueAt(const size_t index) const {
  if (index >= lines_.size()) return {};
  const TaskLine& l = lines_[index];
  if (l.dueEnd <= l.dueBegin) return {};
  return l.raw.substr(l.dueBegin, static_cast<size_t>(l.dueEnd - l.dueBegin));
}

size_t TaskFile::countWithState(const TaskState state) const {
  size_t n = 0;
  for (const auto& l : lines_) {
    if (l.isTask && l.state == state) n++;
  }
  return n;
}

size_t TaskFile::taskCount() const {
  size_t n = 0;
  for (const auto& l : lines_) {
    if (l.isTask) n++;
  }
  return n;
}
