#include "TaskRenderer.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

namespace taskrender {
namespace {

constexpr int kMargin = 22;
constexpr int kRowGap = 8;

std::string stampPath(const std::string& listId) {
  // Leading dot so the stamp never shows up in the list scan.
  return std::string(tasks::kTasksDir) + "/." + listId + ".stamp";
}

// One line: "<hash> <fontId> <hideDone> <pages>".
bool readStamp(const std::string& listId, uint32_t& hash, int& fontId, int& hideDone, int& pages) {
  char buf[96] = {};
  const size_t got = Storage.readFileToBuffer(stampPath(listId).c_str(), buf, sizeof(buf) - 1);
  if (got == 0) return false;
  unsigned long h = 0;
  if (sscanf(buf, "%lu %d %d %d", &h, &fontId, &hideDone, &pages) != 4) return false;
  hash = static_cast<uint32_t>(h);
  return true;
}

void writeStamp(const std::string& listId, const uint32_t hash, const int fontId, const bool hideDone,
                const size_t pages) {
  HalFile f;
  if (!Storage.openFileForWrite("TSK", stampPath(listId), f)) return;
  char buf[96];
  const int n =
      snprintf(buf, sizeof(buf), "%lu %d %d %u\n", (unsigned long)hash, fontId, hideDone ? 1 : 0, (unsigned)pages);
  if (n > 0) f.write(buf, static_cast<size_t>(n));
  f.flush();
}

// A tick inside `box`, drawn rather than blitted so it scales with the font.
void drawTick(const GfxRenderer& r, const int bx, const int by, const int size) {
  const int t = std::max(2, size / 7);
  const int x0 = bx + size / 5;
  const int y0 = by + size / 2;
  const int x1 = bx + size / 2 - size / 10;
  const int y1 = by + size - size / 4;
  const int x2 = bx + size - size / 6;
  const int y2 = by + size / 5;
  r.drawLine(x0, y0, x1, y1, t, true);
  r.drawLine(x1, y1, x2, y2, t, true);
}

void drawStateBox(const GfxRenderer& r, const int x, const int y, const int size, const TaskState state) {
  r.drawRect(x, y, size, size, 2, true);
  if (state == TaskState::InProgress) {
    // The conventional indeterminate glyph: unmistakably neither empty nor ticked.
    const int inset = size / 4;
    const int thickness = std::max(2, size / 5);
    r.fillRect(x + inset, y + (size - thickness) / 2, size - 2 * inset, thickness, true);
  } else if (state == TaskState::Done) {
    drawTick(r, x, y, size);
  }
}

}  // namespace

std::string pagePath(const std::string& listId, const size_t page) {
  return std::string(tasks::kTasksDir) + "/" + listId + "-" + std::to_string(page) + ".bmp";
}

uint32_t contentHash(const TaskFile& file) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (const auto& line : file.lines()) {
    for (const char c : line.raw) {
      h ^= static_cast<uint8_t>(c);
      h *= 16777619u;
    }
    h ^= '\n';
    h *= 16777619u;
  }
  return h;
}

int resolveFontId(GfxRenderer& renderer) {
  const CrossPointSettings& s = SETTINGS;

  if (s.taskSdFontFamilyName[0] != '\0') {
    // The font manager keeps exactly one reader-size font resident, so a task
    // size that differs from the reader's is simply not loaded. Ask for it
    // additively; if the family has no file at that size, fall through.
    const std::vector<uint8_t> sizes = readerFontPointSizes(&sdFontSystem.registry(), s.taskSdFontFamilyName);
    const uint8_t pt = snapToNearestPointSize(sizes, s.taskFontPointSize);
    const int id = sdFontSystem.ensureExtraSize(renderer, s.taskSdFontFamilyName, pt);
    if (id != 0) return id;
    LOG_DBG("TSK", "SD task font %s@%u unavailable, using a built-in", s.taskSdFontFamilyName, (unsigned)pt);
  }

  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), s.taskFontPointSize);
  const bool sans = (s.taskFontFamily == CrossPointSettings::NOTOSANS);
  switch (pt) {
    case 12:
      return sans ? NOTOSANS_12_FONT_ID : NOTOSERIF_12_FONT_ID;
    case 16:
      return sans ? NOTOSANS_16_FONT_ID : NOTOSERIF_16_FONT_ID;
    case 18:
      return sans ? NOTOSANS_18_FONT_ID : NOTOSERIF_18_FONT_ID;
    case 14:
    default:
      return sans ? NOTOSANS_14_FONT_ID : NOTOSERIF_14_FONT_ID;
  }
}

bool needsRender(const TaskFile& file, const std::string& listId, const int fontId, const bool hideDone) {
  uint32_t stampHash = 0;
  int stampFont = 0;
  int stampHide = 0;
  int stampPages = 0;
  if (!readStamp(listId, stampHash, stampFont, stampHide, stampPages)) return true;
  if (stampPages < 1 || !Storage.exists(pagePath(listId, 1).c_str())) return true;
  return stampHash != contentHash(file) || stampFont != fontId || stampHide != (hideDone ? 1 : 0);
}

size_t renderToBmps(GfxRenderer& renderer, const TaskFile& file, const std::string& listId, const int fontId,
                    const bool hideDone) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(fontId);
  const int rowStep = lineHeight + kRowGap;
  const int boxSize = std::max(12, lineHeight - 4);
  const int textX = kMargin + boxSize + 10;

  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID) + 12;
  const int footerHeight = renderer.getLineHeight(SMALL_FONT_ID) + 10;
  const int bodyTop = kMargin + titleHeight;
  const int bodyBottom = h - footerHeight - kMargin / 2;
  const int rowsPerPage = std::max(1, (bodyBottom - bodyTop) / rowStep);

  // The rows to draw, in file order, skipping what the view is hiding.
  std::vector<size_t> rows;
  rows.reserve(file.lineCount());
  for (size_t i = 0; i < file.lineCount(); i++) {
    const TaskLine& l = file.lines()[i];
    if (l.isHeading) {
      rows.push_back(i);
    } else if (l.isTask && !(hideDone && l.state == TaskState::Done)) {
      rows.push_back(i);
    }
  }

  const size_t totalPages =
      rows.empty() ? 1 : std::min(kMaxPages, (rows.size() + rowsPerPage - 1) / static_cast<size_t>(rowsPerPage));

  for (size_t page = 0; page < totalPages; page++) {
    renderer.clearScreen();

    // Title band: the list name, and the page number when there is more than one.
    renderer.drawText(UI_12_FONT_ID, kMargin, kMargin, listId.c_str(), true, EpdFontFamily::BOLD);
    if (totalPages > 1) {
      const std::string pageLabel = std::to_string(page + 1) + "/" + std::to_string(totalPages);
      const int pw = renderer.getTextWidth(UI_12_FONT_ID, pageLabel.c_str());
      renderer.drawText(UI_12_FONT_ID, w - kMargin - pw, kMargin, pageLabel.c_str());
    }
    renderer.drawLine(kMargin, kMargin + titleHeight - 8, w - kMargin, kMargin + titleHeight - 8, 2, true);

    const size_t first = page * static_cast<size_t>(rowsPerPage);
    for (int i = 0; i < rowsPerPage && first + i < rows.size(); i++) {
      const size_t lineIndex = rows[first + i];
      const TaskLine& line = file.lines()[lineIndex];
      const int y = bodyTop + i * rowStep;

      if (line.isHeading) {
        const std::string heading = file.textAt(lineIndex);
        renderer.drawText(fontId, kMargin, y, heading.c_str(), true, EpdFontFamily::BOLD);
        const int hw = renderer.getTextWidth(fontId, heading.c_str(), EpdFontFamily::BOLD);
        renderer.drawLine(kMargin, y + lineHeight, kMargin + hw, y + lineHeight, 1, true);
        continue;
      }

      drawStateBox(renderer, kMargin, y + (lineHeight - boxSize) / 2, boxSize, line.state);

      int available = w - kMargin - textX;
      const std::string due = file.dueAt(lineIndex);
      if (!due.empty()) {
        const int dueWidth = renderer.getTextWidth(SMALL_FONT_ID, due.c_str());
        renderer.drawText(SMALL_FONT_ID, w - kMargin - dueWidth,
                          y + (lineHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, due.c_str());
        available -= dueWidth + 12;
      }

      // In progress reads as bold, done as struck through: the state is legible
      // from the text alone, not only from the box.
      const auto style = line.state == TaskState::InProgress ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const std::string text =
          renderer.truncatedText(fontId, file.textAt(lineIndex).c_str(), std::max(0, available), style);
      renderer.drawText(fontId, textX, y, text.c_str(), true, style);
      if (line.state == TaskState::Done) {
        const int tw = renderer.getTextWidth(fontId, text.c_str(), style);
        const int strikeY = y + lineHeight / 2;
        renderer.drawLine(textX, strikeY, textX + tw, strikeY, 1, true);
      }
    }

    if (rows.empty()) {
      renderer.drawCenteredText(fontId, h / 2, tr(STR_TASK_LIST_EMPTY));
    }

    // Footer: what is left to do, so the screensaver answers the question at a glance.
    const size_t open = file.countWithState(TaskState::Open);
    const size_t doing = file.countWithState(TaskState::InProgress);
    const size_t done = file.countWithState(TaskState::Done);
    char footer[128];
    snprintf(footer, sizeof(footer), "%u %s   %u %s   %u %s", (unsigned)open, tr(STR_TASK_OPEN), (unsigned)doing,
             tr(STR_TASK_IN_PROGRESS), (unsigned)done, tr(STR_TASK_DONE));
    renderer.drawText(SMALL_FONT_ID, kMargin, h - footerHeight, footer);

    if (!ScreenshotUtil::saveFramebufferAsBmp(pagePath(listId, page + 1).c_str(), renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      LOG_ERR("TSK", "Failed to write task page %u for %s", (unsigned)(page + 1), listId.c_str());
      return 0;
    }
  }

  // Drop pages a longer previous render left behind.
  for (size_t p = totalPages + 1; p <= kMaxPages; p++) {
    const std::string path = pagePath(listId, p);
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  }

  writeStamp(listId, contentHash(file), fontId, hideDone, totalPages);
  LOG_DBG("TSK", "Rendered %u page(s) for %s", (unsigned)totalPages, listId.c_str());
  return totalPages;
}

}  // namespace taskrender
