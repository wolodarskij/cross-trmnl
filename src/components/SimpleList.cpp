#include "SimpleList.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

namespace simplelist {

void draw(GfxRenderer& renderer, const Rect rect, const int itemCount, const int selectedIndex,
          const std::function<std::string(int index)>& rowTitle,
          const std::function<std::string(int index)>& rowValue) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = std::max(1, metrics.listRowHeight);
  const int pageItems = std::max(1, rect.height / rowHeight);
  if (itemCount <= 0) return;

  const int safeSelected = std::clamp(selectedIndex, 0, itemCount - 1);
  const int pageStart = (safeSelected / pageItems) * pageItems;
  const int totalPages = (itemCount + pageItems - 1) / pageItems;

  // Overflow arrows at the right edge: ^ at the top, v at the bottom.
  if (totalPages > 1) {
    constexpr int indicatorWidth = 20;
    constexpr int arrowSize = 6;
    constexpr int margin = 15;
    const int centerX = rect.x + rect.width - indicatorWidth / 2 - margin;
    const int indicatorTop = rect.y;
    const int indicatorBottom = rect.y + rect.height - arrowSize;
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + i * 2;
      const int startX = centerX - i;
      renderer.drawLine(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
    }
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
      const int startX = centerX - (arrowSize - 1 - i);
      renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                        indicatorBottom - arrowSize + 1 + i);
    }
  }

  const int padding = metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int minValueGap = 10;

  for (int i = pageStart; i < itemCount && i < pageStart + pageItems; ++i) {
    const int rowY = rect.y + (i - pageStart) * rowHeight;
    const bool selected = i == safeSelected;
    if (selected) {
      renderer.fillRect(rect.x, rowY, rect.width, rowHeight);
    }
    const int textY = rowY + (rowHeight - lineHeight) / 2;
    int titleMaxWidth = rect.width - padding * 2;

    if (rowValue) {
      std::string value = rowValue(i);
      if (!value.empty()) {
        const int maxValueWidth = std::max(0, titleMaxWidth / 2);
        value = renderer.truncatedText(UI_10_FONT_ID, value.c_str(), maxValueWidth);
        const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, value.c_str());
        renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - padding - valueWidth, textY, value.c_str(), !selected);
        titleMaxWidth -= valueWidth + minValueGap;
      }
    }

    const std::string title = renderer.truncatedText(UI_10_FONT_ID, rowTitle(i).c_str(), std::max(0, titleMaxWidth));
    renderer.drawText(UI_10_FONT_ID, rect.x + padding, textY, title.c_str(), !selected);
  }
}

}  // namespace simplelist
