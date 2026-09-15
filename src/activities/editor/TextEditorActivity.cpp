#include "TextEditorActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// NOTOSERIF_14 is the one reader-size family compiled into every build
// (the NOTOSANS set is dropped under OMIT_FONTS), and a fixed built-in font
// keeps the editor free of the SD-font glyph-prewarm dance TxtReader needs.
constexpr int kEditorFontId = NOTOSERIF_14_FONT_ID;
constexpr unsigned long kConfirmLongPressMs = 700;
constexpr unsigned long kBannerMs = 2000;
// Stack read buffer. Deliberately small: loadFile() runs on the main loop task,
// and the heap-discipline skill caps stack buffers well below the KB range.
constexpr size_t kLoadChunkBytes = 256;
// Ghost-cleanup cadence: a HALF refresh after this many FAST ones.
constexpr int kFastRefreshesPerCleanup = 15;
// HID modifier bits that are NOT shift (L/R Ctrl, Alt, GUI). A printable char
// arriving with any of these held is a shortcut, not text to insert.
constexpr uint8_t kNonShiftMods = 0xDD;
// HID modifier bits for either Ctrl.
constexpr uint8_t kCtrlMods = 0x11;

// Options popup rows (order matches the switch in openOptionsPopup's callback).
constexpr StrId kOptionIds[] = {StrId::STR_EDITOR_EDIT_LINE,    StrId::STR_EDITOR_INSERT_BELOW,
                                StrId::STR_EDITOR_INSERT_ABOVE, StrId::STR_EDITOR_DELETE_LINE,
                                StrId::STR_EDITOR_SAVE,         StrId::STR_EDITOR_SAVE_EXIT};
constexpr int kOptionCount = sizeof(kOptionIds) / sizeof(kOptionIds[0]);
}  // namespace

// --- Lifecycle ---------------------------------------------------------------

void TextEditorActivity::onEnter() {
  Activity::onEnter();

  const auto slash = filePath.find_last_of('/');
  fileName = slash == std::string::npos ? filePath : filePath.substr(slash + 1);

  loadFile();

  // Route BLE keyboard keys into the document instead of the button overlay —
  // but only when there IS a document. On an error screen the sink would swallow
  // the keypress meant to dismiss it, leaving a BLE user stuck.
  if (loadError == LoadError::None) {
    mappedInput.setBleTextSink(true);
  }
  requestUpdate();
}

void TextEditorActivity::onExit() {
  mappedInput.setBleTextSink(false);
  // Persist unless the user explicitly discarded. This is the only save that runs
  // when the device sleeps out from under the editor (power button, or the idle
  // timeout while KeyboardEntryActivity is on top and our preventAutoSleep() is
  // not the one being consulted) — ActivityManager calls onExit() on stacked
  // activities during the sleep transition.
  if (dirty && !discardRequested && loadError == LoadError::None) {
    if (saveFile()) {
      LOG_INF("EDT", "Auto-saved %s on exit", filePath.c_str());
    } else {
      LOG_ERR("EDT", "Auto-save on exit FAILED for %s", filePath.c_str());
    }
  }
  lines.clear();
  lines.shrink_to_fit();
  Activity::onExit();
}

// --- File I/O ----------------------------------------------------------------

bool TextEditorActivity::loadFile() {
  lines.clear();
  caretLine = caretCol = topLine = topRow = 0;
  totalBytes = 0;
  dirty = false;
  contentReady = false;
  loadError = LoadError::None;

  if (!Storage.exists(filePath.c_str())) {
    // New file: start with one empty line; the file appears on first save.
    lines.emplace_back();
    contentReady = true;
    return true;
  }

  HalFile f;
  if (!Storage.openFileForRead("EDT", filePath, f)) {
    loadError = LoadError::OpenFailed;
    return false;
  }

  const size_t fileSize = f.fileSize();
  loadFileSize = fileSize;
  if (fileSize > kMaxEditableBytes) {
    loadError = LoadError::TooLarge;
    return false;
  }

  uint8_t buf[kLoadChunkBytes];

  // Pass 1: count lines. The line vector needs lineCount * sizeof(std::string)
  // as ONE contiguous block, so it has to be known and gated before we reserve —
  // growing it instead would need old+new live at once and, under
  // -fno-exceptions, abort rather than fail.
  size_t lineCount = 1;
  size_t scanned = 0;
  while (scanned < fileSize) {
    const int got = f.read(buf, kLoadChunkBytes);
    if (got <= 0) break;
    for (int i = 0; i < got; i++) {
      if (buf[i] == '\n') lineCount++;
    }
    scanned += static_cast<size_t>(got);
  }
  loadLineCount = lineCount;
  if (lineCount > kMaxEditableLines) {
    loadError = LoadError::TooManyLines;
    return false;
  }

  const size_t vectorBytes = lineCount * sizeof(std::string);
  // Free heap must cover the line vector, the text itself, and working margin for
  // edits, the KeyboardEntryActivity round-trip, and the save path. maxAlloc must
  // separately cover the vector's single contiguous block.
  if (ESP.getFreeHeap() < vectorBytes + 2 * fileSize + 24 * 1024 ||
      ESP.getMaxAllocHeap() < vectorBytes + 4 * 1024) {
    LOG_ERR("EDT", "%s: not enough heap (free=%u maxAlloc=%u need vec=%u size=%u)", filePath.c_str(),
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)vectorBytes, (unsigned)fileSize);
    loadError = LoadError::NoHeap;
    return false;
  }

  if (!f.seekSet(0)) {
    loadError = LoadError::OpenFailed;
    return false;
  }

  // Pass 2: build the lines. Exact reserve, so no growth step ever runs.
  lines.reserve(lineCount);
  std::string current;
  size_t readTotal = 0;
  bool sawCrlf = false;
  while (readTotal < fileSize) {
    const int got = f.read(buf, kLoadChunkBytes);
    if (got <= 0) break;
    int segStart = 0;
    for (int i = 0; i < got; i++) {
      if (buf[i] != '\n') continue;
      // Append the run up to the newline in one go rather than byte by byte.
      current.append(reinterpret_cast<const char*>(buf + segStart), static_cast<size_t>(i - segStart));
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
        sawCrlf = true;
      }
      lines.push_back(std::move(current));
      current.clear();
      segStart = i + 1;
    }
    current.append(reinterpret_cast<const char*>(buf + segStart), static_cast<size_t>(got - segStart));
    readTotal += static_cast<size_t>(got);
  }

  if (readTotal != fileSize) {
    LOG_ERR("EDT", "%s: short read %u of %u bytes", filePath.c_str(), (unsigned)readTotal, (unsigned)fileSize);
    loadError = LoadError::OpenFailed;
    lines.clear();
    return false;
  }

  // A file ending in a newline leaves `current` empty here; anything left over is
  // a final line with no terminator. A lone trailing '\r' is content, not a line
  // ending, so it is kept verbatim (only '\r' immediately before '\n' is one).
  hadTrailingNewline = current.empty() && !lines.empty();
  if (!current.empty()) lines.push_back(std::move(current));
  if (lines.empty()) lines.emplace_back();
  // Mixed line endings are normalised to whichever form the file used first;
  // this is the one way an untouched save is not byte-identical.
  usesCrlf = sawCrlf;

  totalBytes = 0;
  for (const auto& l : lines) totalBytes += l.size();
  contentReady = true;
  return true;
}

size_t TextEditorActivity::savedSizeEstimate() const {
  // Text plus the terminators save() will write.
  const size_t eolLen = usesCrlf ? 2 : 1;
  const size_t terminators = lines.empty() ? 0 : (lines.size() - (hadTrailingNewline ? 0 : 1));
  return totalBytes + terminators * eolLen;
}

bool TextEditorActivity::saveFile() {
  // ProgressFile::writeAtomic shape: temp -> flush -> close -> remove + rename,
  // so an interrupted save damages only the throwaway temp file.
  const std::string tmpPath = filePath + ".tmp";
  bool wrote = true;
  {
    HalFile f;
    if (!Storage.openFileForWrite("EDT", tmpPath, f)) {
      LOG_ERR("EDT", "Could not open temp file for write: %s", tmpPath.c_str());
      return false;
    }
    const char* eol = usesCrlf ? "\r\n" : "\n";
    const size_t eolLen = usesCrlf ? 2 : 1;
    for (size_t i = 0; i < lines.size() && wrote; i++) {
      const auto& l = lines[i];
      if (!l.empty() && f.write(l.data(), l.size()) != l.size()) {
        LOG_ERR("EDT", "Short write saving %s", tmpPath.c_str());
        wrote = false;
        break;
      }
      const bool needsEol = i + 1 < lines.size() || hadTrailingNewline;
      if (needsEol && f.write(eol, eolLen) != eolLen) {
        LOG_ERR("EDT", "Short write saving %s", tmpPath.c_str());
        wrote = false;
        break;
      }
    }
    f.flush();
    // f closes at scope exit (DESTRUCTOR_CLOSES_FILE) before the remove/rename.
  }
  if (!wrote) {
    // Never leave a half-written temp lying next to the user's file.
    Storage.remove(tmpPath.c_str());
    return false;
  }

  Storage.remove(filePath.c_str());
  if (!Storage.rename(tmpPath.c_str(), filePath.c_str())) {
    LOG_ERR("EDT", "Failed to rename %s into place", filePath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  dirty = false;
  invalidateTxtReaderCache();
  return true;
}

void TextEditorActivity::invalidateTxtReaderCache() const {
  // TxtReader's page-offset cache only self-invalidates when the file SIZE
  // changes; a same-size edit would serve stale page boundaries. Drop it.
  // (Txt's constructor is pure — it just hashes the path — so this is cheap and
  // safe even for a file that does not exist yet.)
  const Txt txt(filePath, "/.crosspoint");
  const std::string indexPath = txt.getCachePath() + "/index.bin";
  if (Storage.exists(indexPath.c_str())) {
    Storage.remove(indexPath.c_str());
  }
}

void TextEditorActivity::setBanner(const char* text) {
  banner = text ? text : "";
  bannerUntil = millis() + kBannerMs;
}

// --- UTF-8 caret steps -------------------------------------------------------

size_t TextEditorActivity::prevCharStart(const std::string& line, size_t pos) {
  if (pos == 0) return 0;
  pos--;
  while (pos > 0 && (static_cast<unsigned char>(line[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

size_t TextEditorActivity::nextCharEnd(const std::string& line, size_t pos) {
  if (pos >= line.size()) return line.size();
  pos++;
  while (pos < line.size() && (static_cast<unsigned char>(line[pos]) & 0xC0) == 0x80) pos++;
  return pos;
}

// --- Layout ------------------------------------------------------------------

int TextEditorActivity::viewportWidth() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return renderer.getScreenWidth() - metrics.contentSidePadding * 2;
}

void TextEditorActivity::wrapStarts(const std::string& line, std::vector<size_t>& outStarts) const {
  outStarts.clear();
  outStarts.push_back(0);
  if (line.empty()) return;
  const int vw = viewportWidth();
  if (vw <= 0) return;

  size_t start = 0;
  while (start < line.size()) {
    measureBuf.assign(line, start, std::string::npos);
    if (renderer.getTextAdvanceX(kEditorFontId, measureBuf.c_str(), EpdFontFamily::REGULAR) <= vw) break;

    // Longest prefix that fits, by binary search. Measuring down one byte at a
    // time (as TxtReaderActivity does for its one-shot pagination) costs O(n)
    // measurements per row, which is far too slow here: these run per visible
    // line per frame and inside the viewport loops.
    size_t lo = 1;
    size_t hi = line.size() - start;
    while (lo < hi) {
      const size_t mid = lo + (hi - lo + 1) / 2;
      measureBuf.assign(line, start, mid);
      if (renderer.getTextAdvanceX(kEditorFontId, measureBuf.c_str(), EpdFontFamily::REGULAR) <= vw) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    size_t fit = lo;
    // Never split a multi-byte sequence across rows.
    while (fit > 1 && (static_cast<unsigned char>(line[start + fit]) & 0xC0) == 0x80) fit--;

    // Prefer the last space inside the fitting prefix.
    size_t breakLen = fit;
    const size_t sp = line.rfind(' ', start + fit - 1);
    if (sp != std::string::npos && sp > start) breakLen = sp - start;
    if (breakLen == 0) breakLen = 1;

    size_t skip = breakLen;
    if (start + breakLen < line.size() && line[start + breakLen] == ' ') skip++;
    start += skip;
    if (start >= line.size()) break;
    outStarts.push_back(start);
  }
}

int TextEditorActivity::visualRowsForLine(const std::string& line) const {
  wrapStarts(line, rowScratch);
  return static_cast<int>(rowScratch.size());
}

int TextEditorActivity::caretVisualRow() const {
  wrapStarts(lines[caretLine], rowScratch);
  int row = 0;
  for (size_t i = 1; i < rowScratch.size(); i++) {
    if (rowScratch[i] <= caretCol) row = static_cast<int>(i);
  }
  return row;
}

int TextEditorActivity::rowsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(kEditorFontId);
  const int rows = lineHeight > 0 ? contentHeight / lineHeight : 1;
  return rows > 0 ? rows : 1;
}

void TextEditorActivity::ensureCaretVisible() {
  const int pageRows = rowsPerPage();
  const int caretRow = caretVisualRow();

  // Caret above the viewport: anchor the viewport straight onto its row.
  if (caretLine < topLine || (caretLine == topLine && caretRow < static_cast<int>(topRow))) {
    topLine = caretLine;
    topRow = static_cast<size_t>(caretRow);
    return;
  }

  // Rows between the viewport origin and the caret's row.
  int rows = 0;
  for (size_t li = topLine; li < caretLine; ++li) {
    rows += visualRowsForLine(lines[li]);
    if (rows > pageRows + static_cast<int>(topRow)) break;  // already far past; no need to keep counting
  }
  rows += caretRow;
  rows -= static_cast<int>(topRow);

  // Scroll down one VISUAL row at a time — stepping whole source lines would make
  // the tail of a line that wraps to more rows than a page permanently unreachable.
  while (rows >= pageRows) {
    const int topLineRows = visualRowsForLine(lines[topLine]);
    if (static_cast<int>(topRow) + 1 < topLineRows) {
      topRow++;
    } else if (topLine + 1 < lines.size()) {
      topLine++;
      topRow = 0;
    } else {
      break;
    }
    rows--;
  }
}

void TextEditorActivity::pageMove(const bool forward) {
  const int pageRows = rowsPerPage();
  int moved = 0;
  if (forward) {
    while (moved < pageRows) {
      const int topLineRows = visualRowsForLine(lines[topLine]);
      if (static_cast<int>(topRow) + 1 < topLineRows) {
        topRow++;
      } else if (topLine + 1 < lines.size()) {
        topLine++;
        topRow = 0;
      } else {
        break;
      }
      moved++;
    }
  } else {
    while (moved < pageRows) {
      if (topRow > 0) {
        topRow--;
      } else if (topLine > 0) {
        topLine--;
        topRow = static_cast<size_t>(visualRowsForLine(lines[topLine]) - 1);
      } else {
        break;
      }
      moved++;
    }
  }
  // Put the caret at the start of the new top visual row.
  caretLine = topLine;
  wrapStarts(lines[caretLine], rowScratch);
  caretCol = topRow < rowScratch.size() ? rowScratch[topRow] : 0;
  requestUpdate();
}

void TextEditorActivity::clampCaretCol() {
  if (lines.empty()) lines.emplace_back();
  if (caretLine >= lines.size()) caretLine = lines.size() - 1;
  const std::string& line = lines[caretLine];
  if (caretCol > line.size()) caretCol = line.size();
  // Vertical moves carry a byte offset from another line, which can land inside a
  // multi-byte sequence; back up to its start so a later Backspace/Delete cannot
  // slice the sequence in half and corrupt the file.
  while (caretCol > 0 && caretCol < line.size() && (static_cast<unsigned char>(line[caretCol]) & 0xC0) == 0x80) {
    caretCol--;
  }
}

void TextEditorActivity::clampViewport() {
  if (lines.empty()) return;
  if (topLine >= lines.size()) topLine = lines.size() - 1;
  const int rows = visualRowsForLine(lines[topLine]);
  if (static_cast<int>(topRow) >= rows) topRow = static_cast<size_t>(rows - 1);
}

// --- Editing primitives ------------------------------------------------------

void TextEditorActivity::insertCharAtCaret(const char c) {
  if (savedSizeEstimate() + 1 > kMaxEditableBytes) {
    setBanner(tr(STR_EDITOR_FULL));
    return;
  }
  RenderLock lock;
  lines[caretLine].insert(caretCol, 1, c);
  caretCol++;
  totalBytes++;
  dirty = true;
}

void TextEditorActivity::backspaceAtCaret() {
  RenderLock lock;
  if (caretCol > 0) {
    const size_t start = prevCharStart(lines[caretLine], caretCol);
    totalBytes -= caretCol - start;
    lines[caretLine].erase(start, caretCol - start);
    caretCol = start;
    dirty = true;
  } else if (caretLine > 0) {
    // Join with the previous line.
    caretCol = lines[caretLine - 1].size();
    lines[caretLine - 1] += lines[caretLine];
    lines.erase(lines.begin() + caretLine);
    caretLine--;
    dirty = true;
  }
}

void TextEditorActivity::deleteAtCaret() {
  RenderLock lock;
  auto& line = lines[caretLine];
  if (caretCol < line.size()) {
    const size_t end = nextCharEnd(line, caretCol);
    totalBytes -= end - caretCol;
    line.erase(caretCol, end - caretCol);
    dirty = true;
  } else if (caretLine + 1 < lines.size()) {
    line += lines[caretLine + 1];
    lines.erase(lines.begin() + caretLine + 1);
    dirty = true;
  }
}

void TextEditorActivity::splitLineAtCaret() {
  if (lines.size() + 1 > kMaxEditableLines || savedSizeEstimate() + 2 > kMaxEditableBytes) {
    setBanner(tr(STR_EDITOR_FULL));
    return;
  }
  RenderLock lock;
  auto& line = lines[caretLine];
  std::string rest = line.substr(caretCol);
  line.erase(caretCol);
  lines.insert(lines.begin() + caretLine + 1, std::move(rest));
  caretLine++;
  caretCol = 0;
  dirty = true;
}

// --- Sub-activities ----------------------------------------------------------

void TextEditorActivity::openLineEditor() {
  // The line may grow by whatever capacity the rest of the document leaves free.
  const size_t others = totalBytes - lines[caretLine].size();
  const size_t maxLen = kMaxEditableBytes > others ? kMaxEditableBytes - others : 0;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_EDITOR_EDIT_LINE), lines[caretLine],
                                              maxLen),
      [this](const ActivityResult& result) {
        // KeyboardEntryActivity disabled the BLE text sink in its onExit (which
        // ActivityManager runs BEFORE this handler); re-arm it for the document.
        mappedInput.setBleTextSink(true);
        if (!result.isCancelled) {
          const auto& newText = std::get<KeyboardResult>(result.data).text;
          if (newText != lines[caretLine]) {
            RenderLock lock;
            totalBytes = totalBytes - lines[caretLine].size() + newText.size();
            lines[caretLine] = newText;
            dirty = true;
          }
        }
        clampCaretCol();
        clampViewport();
        ensureCaretVisible();
        requestUpdate();
      });
}

void TextEditorActivity::openOptionsPopup() {
  // Hand the keyboard back to the button layer while the modal is up: the popup
  // is driven by logical buttons, which a BLE keyboard reaches through the default
  // keymap only when the text sink is off. Without this the popup is unusable from
  // a keyboard AND every key pressed while it is open is queued in the sink, then
  // replayed into the document the moment it closes.
  mappedInput.setBleTextSink(false);
  optionPopup.show(StrId::STR_TEXT_EDITOR, kOptionIds, kOptionCount, 0, [this](int idx) {
    switch (idx) {
      case 0:  // Edit line
        openLineEditor();
        return;  // openLineEditor re-arms the sink through its result handler
      case 1: {  // Insert line below
        if (lines.size() + 1 > kMaxEditableLines) {
          setBanner(tr(STR_EDITOR_FULL));
          break;
        }
        RenderLock lock;
        lines.insert(lines.begin() + caretLine + 1, std::string());
        caretLine++;
        caretCol = 0;
        dirty = true;
        break;
      }
      case 2: {  // Insert line above
        if (lines.size() + 1 > kMaxEditableLines) {
          setBanner(tr(STR_EDITOR_FULL));
          break;
        }
        RenderLock lock;
        lines.insert(lines.begin() + caretLine, std::string());
        caretCol = 0;
        dirty = true;
        break;
      }
      case 3: {  // Delete line
        RenderLock lock;
        totalBytes -= lines[caretLine].size();
        lines.erase(lines.begin() + caretLine);
        if (lines.empty()) lines.emplace_back();
        if (caretLine >= lines.size()) caretLine = lines.size() - 1;
        caretCol = 0;
        dirty = true;
        break;
      }
      case 4:  // Save
        setBanner(saveFile() ? tr(STR_EDITOR_SAVED) : tr(STR_EDITOR_SAVE_FAILED));
        break;
      case 5:  // Save & exit
        if (saveFile()) {
          finish();
          return;
        }
        setBanner(tr(STR_EDITOR_SAVE_FAILED));
        break;
      default:
        break;
    }
    clampCaretCol();
    clampViewport();
    ensureCaretVisible();
    requestUpdate();
  });
  requestUpdate();
}

void TextEditorActivity::requestExit() {
  if (!dirty) {
    finish();
    return;
  }
  // Same reasoning as openOptionsPopup(): ConfirmationActivity is button-driven,
  // so the sink has to be off for a BLE keyboard to answer it.
  mappedInput.setBleTextSink(false);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_EDITOR_DISCARD), fileName),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          // Confirm = discard: suppress the save-on-exit in onExit().
          discardRequested = true;
          finish();
          return;
        }
        mappedInput.setBleTextSink(true);
        requestUpdate();
      });
}

// --- BLE keyboard ------------------------------------------------------------

bool TextEditorActivity::drainBleKeys() {
  freeink::KeyEvent ev;
  bool changed = false;
  while (mappedInput.popBleTextKey(ev)) {
    // Ctrl+S saves.
    if ((ev.mods & kCtrlMods) != 0 && (ev.ch == 's' || ev.ch == 'S')) {
      setBanner(saveFile() ? tr(STR_EDITOR_SAVED) : tr(STR_EDITOR_SAVE_FAILED));
      changed = true;
      continue;
    }
    // Any other Ctrl/Alt/GUI chord is a shortcut we do not implement — swallow it
    // rather than typing its letter into the document.
    if (ev.ch != 0 && (ev.mods & kNonShiftMods) != 0) continue;

    if (ev.ch != 0) {
      insertCharAtCaret(ev.ch);
      changed = true;
      continue;
    }
    switch (ev.special) {
      case freeink::SpecialKey::Enter:
        splitLineAtCaret();
        changed = true;
        break;
      case freeink::SpecialKey::Backspace:
        backspaceAtCaret();
        changed = true;
        break;
      case freeink::SpecialKey::Delete:
        deleteAtCaret();
        changed = true;
        break;
      case freeink::SpecialKey::Escape:
        requestExit();
        return false;
      case freeink::SpecialKey::Left:
        if (caretCol > 0) {
          caretCol = prevCharStart(lines[caretLine], caretCol);
        } else if (caretLine > 0) {
          caretLine--;
          caretCol = lines[caretLine].size();
        }
        changed = true;
        break;
      case freeink::SpecialKey::Right:
        if (caretCol < lines[caretLine].size()) {
          caretCol = nextCharEnd(lines[caretLine], caretCol);
        } else if (caretLine + 1 < lines.size()) {
          caretLine++;
          caretCol = 0;
        }
        changed = true;
        break;
      case freeink::SpecialKey::Up:
        if (caretLine > 0) caretLine--;
        clampCaretCol();
        changed = true;
        break;
      case freeink::SpecialKey::Down:
        if (caretLine + 1 < lines.size()) caretLine++;
        clampCaretCol();
        changed = true;
        break;
      case freeink::SpecialKey::Home:
        caretCol = 0;
        changed = true;
        break;
      case freeink::SpecialKey::End:
        caretCol = lines[caretLine].size();
        changed = true;
        break;
      case freeink::SpecialKey::PageUp:
        pageMove(false);
        break;
      case freeink::SpecialKey::PageDown:
        pageMove(true);
        break;
      default:
        break;
    }
  }
  if (changed) {
    clampViewport();
    ensureCaretVisible();
    // One repaint per drained batch: burst typing latches in the 16-deep rings
    // and lands in a single e-ink refresh instead of one per keystroke.
    requestUpdate();
  }
  return true;
}

// --- Main loop ---------------------------------------------------------------

void TextEditorActivity::loop() {
  if (loadError != LoadError::None) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }

  if (bannerUntil > 0 && millis() > bannerUntil) {
    banner.clear();
    bannerUntil = 0;
    requestUpdate();
  }

  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    popupClosing = !optionPopup.isActive();
    if (popupClosing && !lines.empty()) {
      // Modal closed: take the keyboard back for the document. (Skipped when the
      // callback started a sub-activity, which re-arms in its own handler.)
      mappedInput.setBleTextSink(true);
    }
    return;
  }
  if (popupClosing) {
    // Swallow the trailing release of the press that closed the popup.
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;
    }
    popupClosing = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;
    }
  }

  if (!drainBleKeys()) return;

  // Long-press Confirm -> options popup; short release -> edit the caret line.
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }
  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= kConfirmLongPressMs) {
    confirmLongHandled = true;
    openOptionsPopup();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool fire = confirmHeld && !confirmLongHandled;
    confirmHeld = false;
    confirmLongHandled = false;
    if (fire) openLineEditor();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    requestExit();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (caretLine + 1 < lines.size()) {
      caretLine++;
      clampCaretCol();
      ensureCaretVisible();
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousRelease([this] {
    if (caretLine > 0) {
      caretLine--;
      clampCaretCol();
      ensureCaretVisible();
      requestUpdate();
    }
  });
  if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    pageMove(true);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    pageMove(false);
  }
}

// --- Render ------------------------------------------------------------------

void TextEditorActivity::render(RenderLock&&) {
  // The popup composites over whatever the panel already holds and pushes it
  // itself, so it must run before clearScreen() — same order as SettingsActivity.
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (loadError != LoadError::None) {
    const char* msg = loadError == LoadError::TooLarge       ? tr(STR_EDITOR_TOO_LARGE)
                      : loadError == LoadError::TooManyLines ? tr(STR_EDITOR_TOO_MANY_LINES)
                      : loadError == LoadError::NoHeap       ? tr(STR_EDITOR_NO_HEAP)
                                                             : tr(STR_EDITOR_LOAD_FAILED);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, fileName.c_str());
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - renderer.getLineHeight(UI_12_FONT_ID), msg);
    char detail[64];
    detail[0] = '\0';
    if (loadError == LoadError::TooLarge) {
      snprintf(detail, sizeof(detail), "%u KB > %u KB", (unsigned)(loadFileSize / 1024),
               (unsigned)(kMaxEditableBytes / 1024));
    } else if (loadError == LoadError::TooManyLines) {
      snprintf(detail, sizeof(detail), "%u > %u", (unsigned)loadLineCount, (unsigned)kMaxEditableLines);
    }
    if (detail[0] != '\0') {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 6, detail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Header: filename, with a dirty marker.
  std::string title = fileName;
  if (dirty) title += " *";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(kEditorFontId);
  const int x0 = metrics.contentSidePadding;

  int y = contentTop;
  // contentReady guards against rendering while onEnter() is still filling `lines`
  // (render runs on its own task and a notification can already be pending).
  for (size_t li = topLine; contentReady && li < lines.size() && y + lineHeight <= contentBottom; ++li) {
    const std::string& line = lines[li];
    wrapStarts(line, renderScratch);
    // Only the first drawn line starts mid-way, and only when the viewport is
    // scrolled inside it.
    size_t firstSeg = (li == topLine && topRow < renderScratch.size()) ? topRow : 0;
    for (size_t i = firstSeg; i < renderScratch.size() && y + lineHeight <= contentBottom; ++i) {
      const size_t segStart = renderScratch[i];
      const size_t segEnd = i + 1 < renderScratch.size() ? renderScratch[i + 1] : line.size();
      if (segEnd > segStart) {
        measureBuf.assign(line, segStart, segEnd - segStart);
        renderer.drawText(kEditorFontId, x0, y, measureBuf.c_str());
      }
      // Caret: on its visual row (the last row whose start <= caretCol).
      const bool caretHere = li == caretLine && caretCol >= segStart &&
                             (caretCol < segEnd || (i + 1 == renderScratch.size() && caretCol <= segEnd));
      if (caretHere) {
        measureBuf.assign(line, segStart, caretCol - segStart);
        const int caretX = x0 + renderer.getTextAdvanceX(kEditorFontId, measureBuf.c_str(), EpdFontFamily::REGULAR);
        renderer.fillRect(caretX, y, 2, lineHeight);
      }
      y += lineHeight;
    }
  }

  // Transient status ("Saved" / "Editor full"), above the hints.
  if (!banner.empty()) {
    GUI.drawHelpText(renderer, Rect{0, contentBottom - 20, pageWidth, 20}, banner.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_EDIT_BUTTON), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Periodic ghost cleanup, matching the readers' refresh hygiene.
  if (++fastRefreshCount >= kFastRefreshesPerCleanup) {
    fastRefreshCount = 0;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer();
  }
}
