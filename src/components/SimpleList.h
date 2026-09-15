#pragma once

#include <functional>
#include <string>

class GfxRenderer;
struct Rect;

// Minimal button-driven list for screens that do not host a FreeInkUI app
// (the Bluetooth settings views and the remote-button mapper). Rows are
// metrics.listRowHeight tall, the list pages by whole screens as the selection
// moves past the last visible row, the selected row is drawn inverted, and an
// optional value is right-aligned in the row. No touch handling: these screens
// are driven by the front buttons (and the BLE keyboard) only.
namespace simplelist {

void draw(GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
          const std::function<std::string(int index)>& rowTitle,
          const std::function<std::string(int index)>& rowValue = nullptr);

}  // namespace simplelist
