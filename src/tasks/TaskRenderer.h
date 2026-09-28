#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "TaskFile.h"

class GfxRenderer;

// Renders a task list to full-screen BMP pages on the SD card.
//
// The pages land next to the list as /tasks/<listId>-1.bmp, -2.bmp, ... They
// are ordinary 1-bit BMPs of the panel's own size, which is what lets the sleep
// screen reuse the dashboard image path verbatim and lets the BMP viewer open
// them.
//
// Rendering goes through the framebuffer: draw a page, serialise it with
// ScreenshotUtil::saveFramebufferAsBmp, repeat. Callers with something on
// screen must bracket the call in storeBwBuffer()/restoreBwBuffer() — the
// framebuffer is the only one there is.
namespace taskrender {

// A long list is capped rather than allowed to spray dozens of files across the
// card. Beyond this the remaining tasks are simply not rendered; the on-device
// list still shows them all.
constexpr size_t kMaxPages = 8;

// "/tasks/<listId>-<page>.bmp", page being 1-based.
std::string pagePath(const std::string& listId, size_t page);

// Font id for task pages, honouring the task font settings. Loads an SD family
// at the wanted size when the reader holds a different one (the font manager
// keeps only one reader-size font resident), and falls back to a built-in Noto
// face when that is not possible.
int resolveFontId(GfxRenderer& renderer);

// FNV-1a over every line's bytes. Cheap, allocation-free, and — unlike the file
// size — it actually changes when a task is ticked, which is the whole point.
uint32_t contentHash(const TaskFile& file);

// True when the pages are missing, or were rendered from different content,
// different font settings, or a different hide-done choice.
bool needsRender(const TaskFile& file, const std::string& listId, int fontId, bool hideDone);

// Draws the list and writes the pages. Returns the number of pages written, or
// 0 on failure. Deletes pages left over from a previous, longer render so a
// stale page 3 never outlives the list.
size_t renderToBmps(GfxRenderer& renderer, const TaskFile& file, const std::string& listId, int fontId, bool hideDone);

}  // namespace taskrender
