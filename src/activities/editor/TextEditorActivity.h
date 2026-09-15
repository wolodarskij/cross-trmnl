#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Simple on-device .txt/.md editor (fork-local feature; upstream SCOPE.md
// deliberately excludes notepads).
//
// Buffer model: the whole file as a vector of logical lines — many small
// allocations instead of one large block, per the heap-discipline skill. Entry
// is gated on file size, on LINE COUNT (the line vector itself needs one
// contiguous block of lineCount * sizeof(std::string)), and on both free heap
// and largest-free-block. Larger files stay readable through TxtReaderActivity
// but refuse to open here.
//
// Editing surfaces:
//  - Buttons only: Up/Down move the caret line, Confirm edits the current line
//    through KeyboardEntryActivity (the whole OSK is reused — this activity
//    never re-implements typing), long-Confirm opens a line/file options popup,
//    Back prompts when dirty.
//  - BLE keyboard: the MappedInputManager text sink delivers full key events —
//    printables insert at the caret, Enter splits, Backspace/Delete join and
//    erase, arrows/Home/End/PageUp/PageDown navigate, Ctrl+S saves, Escape
//    exits (with the same dirty prompt).
//
// Saving is crash-safe in the ProgressFile::writeAtomic shape: write
// `<path>.tmp`, flush, close, remove + rename, and drop the temp on any failure
// so a half-written file is never left behind. After a successful save the
// TxtReader page-index cache for this file is dropped — it only self-invalidates
// on file-size change, so a same-size edit would otherwise serve stale pages.
//
// Unsaved work is never silently lost: onExit() writes the buffer back unless
// the user explicitly chose Discard. That matters because the device can be put
// to sleep (power button, or idle while a sub-activity is on top) without this
// activity's loop() getting a say, and ActivityManager runs onExit() on stacked
// activities during the sleep transition.
class TextEditorActivity final : public Activity {
 public:
  explicit TextEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
      : Activity("TextEditor", renderer, mappedInput), filePath(std::move(filePath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Keep a connected BLE keyboard usable while editing, wherever we were opened from.
  bool keepsBluetoothAlive() const override { return true; }
  // Hold off the idle timer while there are unsaved edits. (onExit() still saves,
  // so this is comfort rather than the safety net.)
  bool preventAutoSleep() override { return dirty; }

  // Ceiling for opening a file in the editor. The whole document lives in RAM as
  // per-line strings; beyond this the "file too large" screen shows instead.
  // (TxtReaderActivity streams and has no such limit.)
  static constexpr size_t kMaxEditableBytes = 32 * 1024;
  // Second ceiling, on line COUNT: std::vector<std::string> needs one contiguous
  // block of kMaxEditableLines * sizeof(std::string) (~24 B each on this
  // toolchain). Without this cap a 32 KB file of one-character lines would ask
  // for a ~750 KB block and, under -fno-exceptions, abort the firmware instead
  // of failing gracefully.
  static constexpr size_t kMaxEditableLines = 1200;

 private:
  enum class LoadError : uint8_t { None, TooLarge, TooManyLines, NoHeap, OpenFailed };

  std::string filePath;
  std::string fileName;  // basename, for the header
  std::vector<std::string> lines;
  size_t caretLine = 0;
  size_t caretCol = 0;  // byte offset within lines[caretLine]
  size_t topLine = 0;   // first source line drawn
  size_t topRow = 0;    // first VISUAL row of topLine drawn (intra-line scroll)
  size_t totalBytes = 0;
  bool dirty = false;
  bool usesCrlf = false;            // original file used \r\n; preserved on save
  bool hadTrailingNewline = false;  // original file ended with a newline; preserved
  // Set once the buffer is fully loaded. render() runs on the render task and can
  // fire while onEnter() is still filling `lines`; this keeps it from iterating a
  // vector that is being reallocated underneath it.
  bool contentReady = false;
  // Set when the user answers "Discard changes?" affirmatively, so onExit() knows
  // not to write the buffer back.
  bool discardRequested = false;
  LoadError loadError = LoadError::None;
  size_t loadFileSize = 0;   // for the too-large message
  size_t loadLineCount = 0;  // for the too-many-lines message

  // Long-press bookkeeping for Confirm (mirrors KeyboardEntryActivity's pattern).
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  // Swallow the Confirm release that closed the options popup.
  bool popupClosing = false;

  // Transient status line ("Saved" / "Save failed").
  std::string banner;
  unsigned long bannerUntil = 0;

  // Periodic ghost cleanup: FAST refreshes since the last HALF one.
  int fastRefreshCount = 0;

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;

  // Reusable scratch so the wrap helpers don't allocate on every call (they run
  // per visible line per frame, and in the viewport loops).
  mutable std::string measureBuf;
  mutable std::vector<size_t> rowScratch;     // visualRowsForLine / caret math
  mutable std::vector<size_t> renderScratch;  // render()

  bool loadFile();
  bool saveFile();
  void invalidateTxtReaderCache() const;
  void setBanner(const char* text);
  // Bytes this document would occupy on disk, including line terminators.
  size_t savedSizeEstimate() const;

  // Returns false when a drained BLE key finished the activity.
  bool drainBleKeys();
  void insertCharAtCaret(char c);
  void backspaceAtCaret();
  void deleteAtCaret();
  void splitLineAtCaret();
  void openLineEditor();
  void openOptionsPopup();
  void requestExit();

  // UTF-8-aware caret steps within the current line.
  static size_t prevCharStart(const std::string& line, size_t pos);
  static size_t nextCharEnd(const std::string& line, size_t pos);

  // Wrapped layout of one source line: byte offsets where each visual row starts
  // (first entry always 0). Pure function of the line + viewport, used by both
  // render() and the caret-visibility/paging math.
  void wrapStarts(const std::string& line, std::vector<size_t>& outStarts) const;
  int visualRowsForLine(const std::string& line) const;
  int caretVisualRow() const;
  int rowsPerPage() const;
  int viewportWidth() const;
  void ensureCaretVisible();
  void pageMove(bool forward);
  void clampCaretCol();
  void clampViewport();
};
