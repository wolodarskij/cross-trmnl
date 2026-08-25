#include "ScriptBindings.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MappedInputManager.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "CrossPointSettings.h"
#include "LuaStrip.h"
#include "ScriptEngine.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/WifiConnector.h"

namespace {

using Button = MappedInputManager::Button;

ScriptContext* ctx(lua_State* L) { return *static_cast<ScriptContext**>(lua_getextraspace(L)); }
GfxRenderer& gfx(lua_State* L) { return *ctx(L)->renderer; }

// ---- helpers -------------------------------------------------------------

int fontForSize(const char* name) {
  if (!name) return UI_10_FONT_ID;
  if (!strcmp(name, "small") || !strcmp(name, "s")) return SMALL_FONT_ID;
  if (!strcmp(name, "large") || !strcmp(name, "l") || !strcmp(name, "xl")) return UI_12_FONT_ID;
  return UI_10_FONT_ID;  // "medium"/default
}

// Map a Lua ink argument onto the renderer's 4-level palette (GfxRenderer.h),
// the same vocabulary the rest of the UI draws with. Booleans keep their old
// meaning — true/absent = black, false = white — so every existing call site is
// unaffected. An unrecognized name falls back to the default, as fontForSize does.
Color inkArg(lua_State* L, int idx, Color dflt) {
  if (lua_isnoneornil(L, idx)) return dflt;
  if (lua_isboolean(L, idx)) return lua_toboolean(L, idx) ? Color::Black : Color::White;
  const char* name = lua_tostring(L, idx);
  if (!name) return dflt;
  struct Map {
    const char* n;
    Color c;
  };
  // Both spellings: the panel is documented in en-GB but the enum is en-US.
  static const Map kMap[] = {{"black", Color::Black},         {"darkgray", Color::DarkGray},
                             {"darkgrey", Color::DarkGray},   {"lightgray", Color::LightGray},
                             {"lightgrey", Color::LightGray}, {"white", Color::White}};
  for (const auto& m : kMap) {
    if (!strcmp(name, m.n)) return m.c;
  }
  return dflt;
}

bool buttonFromName(const char* name, Button& out) {
  if (!name) return false;
  struct Map {
    const char* n;
    Button b;
  };
  static const Map kMap[] = {{"back", Button::Back},   {"confirm", Button::Confirm}, {"left", Button::Left},
                             {"right", Button::Right}, {"up", Button::Up},           {"down", Button::Down},
                             {"power", Button::Power}};
  for (const auto& m : kMap) {
    if (!strcmp(name, m.n)) {
      out = m.b;
      return true;
    }
  }
  return false;
}

// Writes confined to /scripts so a script cannot clobber system files. Reads
// are unrestricted. Returns false (and leaves a Lua error pushed) on escape.
bool writablePath(lua_State* L, const char* raw, std::string& out) {
  std::string p = raw ? raw : "";
  if (p.empty()) {
    lua_pushstring(L, "empty path");
    return false;
  }
  if (p[0] != '/') p = "/scripts/" + p;  // relative paths land in /scripts
  if (p.find("..") != std::string::npos) {
    lua_pushstring(L, "'..' not allowed in path");
    return false;
  }
  if (p.rfind("/scripts/", 0) != 0 && p != "/scripts") {
    lua_pushstring(L, "writes are limited to /scripts");
    return false;
  }
  out = p;
  return true;
}

// ---- screen --------------------------------------------------------------

int l_screen_clear(lua_State* L) {
  // A palette name fills the whole screen at that level; a number stays a raw
  // framebuffer byte (memset across the buffer), as it has always been.
  if (lua_isstring(L, 1) && !lua_isnumber(L, 1)) {
    gfx(L).fillRectDither(0, 0, gfx(L).getScreenWidth(), gfx(L).getScreenHeight(), inkArg(L, 1, Color::White));
    return 0;
  }
  const int color = static_cast<int>(luaL_optinteger(L, 1, 0xFF));
  gfx(L).clearScreen(static_cast<uint8_t>(color));
  return 0;
}

int l_screen_text(lua_State* L) {
  const int x = static_cast<int>(luaL_checkinteger(L, 1));
  const int y = static_cast<int>(luaL_checkinteger(L, 2));
  const char* text = luaL_checkstring(L, 3);

  int fontId = UI_10_FONT_ID;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  const char* align = "left";
  if (lua_istable(L, 4)) {
    lua_getfield(L, 4, "size");
    fontId = fontForSize(lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, 4, "bold");
    if (lua_toboolean(L, -1)) style = EpdFontFamily::BOLD;
    lua_pop(L, 1);
    lua_getfield(L, 4, "align");
    if (lua_isstring(L, -1)) align = lua_tostring(L, -1);
    lua_pop(L, 1);
  }

  if (!strcmp(align, "center")) {
    gfx(L).drawCenteredText(fontId, y, text, true, style);
  } else if (!strcmp(align, "right")) {
    const int w = gfx(L).getTextWidth(fontId, text, style);
    gfx(L).drawText(fontId, x - w, y, text, true, style);
  } else {
    gfx(L).drawText(fontId, x, y, text, true, style);
  }
  return 0;
}

int l_screen_line(lua_State* L) {
  const int x1 = static_cast<int>(luaL_checkinteger(L, 1));
  const int y1 = static_cast<int>(luaL_checkinteger(L, 2));
  const int x2 = static_cast<int>(luaL_checkinteger(L, 3));
  const int y2 = static_cast<int>(luaL_checkinteger(L, 4));
  const int width = static_cast<int>(luaL_optinteger(L, 5, 1));
  if (width > 1) {
    gfx(L).drawLine(x1, y1, x2, y2, width, true);
  } else {
    gfx(L).drawLine(x1, y1, x2, y2, true);
  }
  return 0;
}

int l_screen_rect(lua_State* L) {
  const int x = static_cast<int>(luaL_checkinteger(L, 1));
  const int y = static_cast<int>(luaL_checkinteger(L, 2));
  const int w = static_cast<int>(luaL_checkinteger(L, 3));
  const int h = static_cast<int>(luaL_checkinteger(L, 4));
  const bool fill = lua_toboolean(L, 5);
  // Optional 6th arg: ink when filled — true/omitted = black, false = white
  // (used to clear under labels drawn over sprites), or a palette name for one
  // of the four dithered levels.
  if (fill) {
    gfx(L).fillRectDither(x, y, w, h, inkArg(L, 6, Color::Black));
  } else {
    gfx(L).drawRect(x, y, w, h, true);
  }
  return 0;
}

int l_screen_pixel(lua_State* L) {
  const int x = static_cast<int>(luaL_checkinteger(L, 1));
  const int y = static_cast<int>(luaL_checkinteger(L, 2));
  // 3rd arg: true/omitted = black, false = white, or a palette name. A single
  // pixel at a gray level only inks where that level's dither pattern falls.
  gfx(L).drawPixelDither(x, y, inkArg(L, 3, Color::Black));
  return 0;
}

int l_screen_image(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  const int x = static_cast<int>(luaL_checkinteger(L, 2));
  const int y = static_cast<int>(luaL_checkinteger(L, 3));
  const int w = static_cast<int>(luaL_optinteger(L, 4, gfx(L).getScreenWidth()));
  const int h = static_cast<int>(luaL_optinteger(L, 5, gfx(L).getScreenHeight()));

  HalFile file;
  if (!Storage.openFileForRead("LUA", path, file)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  Bitmap bitmap(file, true);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) gfx(L).drawBitmap(bitmap, x, y, w, h, 0, 0);
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

// screen.save(path [, x, y, w, h]) -> ok. Write a region of the framebuffer
// (default: the whole screen) as a 1-bit BMP — the same format screen.image
// reads back, so a script can round-trip its own output (paint.lua's save).
// The region is clipped to the screen; the write is confined to /scripts like
// every other script write. Rows stream through one stack buffer, so the
// screen-sized image never exists in RAM — the Lua heap sees nothing at all.
int l_screen_save(lua_State* L) {
  std::string path;
  if (!writablePath(L, luaL_checkstring(L, 1), path)) return lua_error(L);
  GfxRenderer& g = gfx(L);
  const int sw = g.getScreenWidth();
  const int sh = g.getScreenHeight();
  int x = static_cast<int>(luaL_optinteger(L, 2, 0));
  int y = static_cast<int>(luaL_optinteger(L, 3, 0));
  int w = static_cast<int>(luaL_optinteger(L, 4, sw));
  int h = static_cast<int>(luaL_optinteger(L, 5, sh));
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > sw) w = sw - x;
  if (y + h > sh) h = sh - y;
  if (w <= 0 || h <= 0) return luaL_error(L, "screen.save: empty region");

  const int rowBytes = (w + 31) / 32 * 4;  // BMP rows pad to 4 bytes
  uint8_t row[128];                        // widest logical row is 800 px -> 100 bytes
  static_assert(sizeof(row) >= (800 + 31) / 32 * 4, "row buffer must hold the widest screen row");

  BmpHeader hdr = {};
  hdr.fileHeader.bfType = 0x4D42;  // "BM"
  hdr.fileHeader.bfOffBits = sizeof(BmpHeader);
  hdr.fileHeader.bfSize = sizeof(BmpHeader) + static_cast<uint32_t>(rowBytes) * h;
  hdr.infoHeader.biSize = 40;
  hdr.infoHeader.biWidth = w;
  hdr.infoHeader.biHeight = h;  // positive: bottom-up rows
  hdr.infoHeader.biPlanes = 1;
  hdr.infoHeader.biBitCount = 1;
  hdr.infoHeader.biSizeImage = static_cast<uint32_t>(rowBytes) * h;
  hdr.infoHeader.biClrUsed = 2;
  hdr.colors[0] = {255, 255, 255, 0};  // bit 0 = white
  hdr.colors[1] = {0, 0, 0, 0};        // bit 1 = black

  HalFile f;
  if (!Storage.openFileForWrite("LUA", path.c_str(), f)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  bool ok = f.write(&hdr, sizeof(hdr)) == sizeof(hdr);
  for (int yy = y + h - 1; ok && yy >= y; --yy) {
    memset(row, 0, rowBytes);
    for (int i = 0; i < w; ++i) {
      if (g.getPixel(x + i, yy)) row[i >> 3] |= 0x80 >> (i & 7);
    }
    ok = f.write(row, rowBytes) == static_cast<size_t>(rowBytes);
  }
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int l_screen_width(lua_State* L) {
  lua_pushinteger(L, gfx(L).getScreenWidth());
  return 1;
}
int l_screen_height(lua_State* L) {
  lua_pushinteger(L, gfx(L).getScreenHeight());
  return 1;
}
int l_screen_invert(lua_State* L) {
  gfx(L).invertScreen();
  return 0;
}

int l_screen_update(lua_State* L) {
  HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH;
  const char* m = lua_tostring(L, 1);
  if (m) {
    if (!strcmp(m, "full")) mode = HalDisplay::FULL_REFRESH;
    else if (!strcmp(m, "half")) mode = HalDisplay::HALF_REFRESH;
  }
  gfx(L).displayBuffer(mode);
  return 0;
}

// ---- input ---------------------------------------------------------------

// Non-Back buttons only. Soft Back is delivered via ScriptContext::backSoftPending
// after a short press; long-hold Back force-aborts in ScriptEngine::pump.
const char* pressedButtonName(MappedInputManager& in) {
  struct Map {
    Button b;
    const char* n;
  };
  static const Map kMap[] = {{Button::Confirm, "confirm"}, {Button::Left, "left"}, {Button::Right, "right"},
                             {Button::Up, "up"},           {Button::Down, "down"}, {Button::Power, "power"}};
  for (const auto& m : kMap) {
    if (in.wasPressed(m.b)) return m.n;
  }
  return nullptr;
}

// Blocks until a button is pressed. Short Back → "back"; long-hold Back aborts.
int l_input_wait(lua_State* L) {
  ScriptContext* c = ctx(L);
  MappedInputManager& in = *c->input;
  for (;;) {
    if (ScriptEngine::pump(L)) return luaL_error(L, "aborted");
    if (c->backSoftPending) {
      c->backSoftPending = false;
      lua_pushstring(L, "back");
      return 1;
    }
    const char* name = pressedButtonName(in);
    if (name) {
      lua_pushstring(L, name);
      return 1;
    }
    delay(15);
  }
}

// Non-blocking: returns a button name (including soft "back") or nil.
int l_input_poll(lua_State* L) {
  ScriptContext* c = ctx(L);
  if (ScriptEngine::pump(L)) return luaL_error(L, "aborted");
  if (c->backSoftPending) {
    c->backSoftPending = false;
    lua_pushstring(L, "back");
    return 1;
  }
  const char* name = pressedButtonName(*c->input);
  if (name) {
    lua_pushstring(L, name);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int l_input_down(lua_State* L) {
  Button b;
  if (!buttonFromName(luaL_checkstring(L, 1), b)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  ctx(L)->input->update();
  lua_pushboolean(L, ctx(L)->input->isPressed(b) ? 1 : 0);
  return 1;
}

// ---- fs ------------------------------------------------------------------

int l_fs_read(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (!Storage.exists(path)) {
    lua_pushnil(L);
    return 1;
  }
  bool truncated = false;
  size_t fileSize = 0;
  const String content = Storage.readFile(path, &truncated, &fileSize);
  // Raise rather than return the short string. `nil` already means "missing"
  // here, and scripts routinely write `fs.read(p) or default` — either way a
  // half file would be swallowed, which is the failure this guards against.
  if (truncated) {
    return luaL_error(L, "fs.read: %s", scriptbindings::tooLargeMessage(path, fileSize).c_str());
  }
  lua_pushlstring(L, content.c_str(), content.length());
  return 1;
}

// fs.readRange(path, offset, len) -> string | nil (missing file).
//
// The piecewise counterpart of fs.read, for files that are deliberately bigger
// than a script's memory: a packed text sidecar is read one record at a time
// and never whole, the same way screen.image streams rows. The cap is the
// point — a range read exists to keep large buffers out of the heap, so a
// large range through it is a bug in the caller, not a request to honor.
constexpr lua_Integer kMaxRangeRead = 4096;

int l_fs_readrange(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  const lua_Integer offset = luaL_checkinteger(L, 2);
  const lua_Integer len = luaL_checkinteger(L, 3);
  if (offset < 0) return luaL_error(L, "fs.readRange: negative offset (%d)", (int)offset);
  if (len < 0 || len > kMaxRangeRead) {
    return luaL_error(L, "fs.readRange: len %d out of range (0..%d)", (int)len, (int)kMaxRangeRead);
  }
  if (!Storage.exists(path)) {
    lua_pushnil(L);
    return 1;
  }
  auto f = Storage.open(path);
  if (!f) {
    lua_pushnil(L);
    return 1;
  }
  if (len == 0) {
    f.close();
    lua_pushliteral(L, "");
    return 1;
  }
  luaL_Buffer b;
  char* out = luaL_buffinitsize(L, &b, (size_t)len);
  int got = -1;
  if (f.seekSet((size_t)offset)) got = f.read(out, (size_t)len);
  f.close();
  // Same philosophy as the whole-file truncation guard: a short read means the
  // caller's offsets are wrong, and half a record must never look like a whole
  // one. `nil` stays reserved for "no such file".
  if (got != (int)len) {
    return luaL_error(L, "fs.readRange: %s: wanted %d bytes at %d, got %d", path, (int)len, (int)offset, got);
  }
  luaL_pushresultsize(&b, (size_t)len);
  return 1;
}

int l_fs_write(lua_State* L) {
  std::string path;
  if (!writablePath(L, luaL_checkstring(L, 1), path)) return lua_error(L);
  size_t len = 0;
  const char* data = luaL_checklstring(L, 2, &len);
  const bool ok = Storage.writeFile(path.c_str(), String(data));
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int l_fs_append(lua_State* L) {
  std::string path;
  if (!writablePath(L, luaL_checkstring(L, 1), path)) return lua_error(L);
  const char* data = luaL_checkstring(L, 2);
  // Append is read-modify-write, so a short read here does not merely lose the
  // tail — it writes the truncation back and destroys it. Refuse instead.
  bool truncated = false;
  size_t fileSize = 0;
  String combined = Storage.exists(path.c_str()) ? Storage.readFile(path.c_str(), &truncated, &fileSize) : String();
  if (truncated) {
    return luaL_error(L, "fs.append refuses to rewrite the file truncated: %s",
                      scriptbindings::tooLargeMessage(path.c_str(), fileSize).c_str());
  }
  combined += data;
  lua_pushboolean(L, Storage.writeFile(path.c_str(), combined) ? 1 : 0);
  return 1;
}

int l_fs_remove(lua_State* L) {
  std::string path;
  if (!writablePath(L, luaL_checkstring(L, 1), path)) return lua_error(L);
  lua_pushboolean(L, Storage.remove(path.c_str()) ? 1 : 0);
  return 1;
}

int l_fs_mkdir(lua_State* L) {
  std::string path;
  if (!writablePath(L, luaL_checkstring(L, 1), path)) return lua_error(L);
  lua_pushboolean(L, Storage.mkdir(path.c_str()) ? 1 : 0);
  return 1;
}

int l_fs_exists(lua_State* L) {
  lua_pushboolean(L, Storage.exists(luaL_checkstring(L, 1)) ? 1 : 0);
  return 1;
}

int l_fs_list(lua_State* L) {
  const char* dir = luaL_optstring(L, 1, "/scripts");
  lua_newtable(L);
  auto root = Storage.open(dir);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return 1;  // empty table
  }
  int idx = 1;
  char name[256];
  for (auto f = root.openNextFile(); f; f = root.openNextFile()) {
    f.getName(name, sizeof(name));
    if (name[0] != '.') {
      std::string entry = name;
      if (f.isDirectory()) entry += "/";
      lua_pushstring(L, entry.c_str());
      lua_rawseti(L, -2, idx++);
    }
    f.close();
  }
  root.close();
  return 1;
}

// ---- http ----------------------------------------------------------------

int l_http_get(lua_State* L) {
  const char* url = luaL_checkstring(L, 1);
  const bool wasConnected = WiFi.status() == WL_CONNECTED;
  if (!wasConnected) {
    if (!WifiConnector::connectToSaved()) {
      lua_pushnil(L);
      lua_pushstring(L, "wifi connect failed");
      return 2;
    }
    ctx(L)->wifiStartedByScript = true;  // ScriptRunActivity tears it down on exit
  }

  std::string body;
  const bool ok = HttpDownloader::fetchUrl(std::string(url), body);
  if (!ok) {
    lua_pushnil(L);
    lua_pushstring(L, "http request failed");
    return 2;
  }
  lua_pushlstring(L, body.data(), body.size());
  return 1;
}

// ---- device --------------------------------------------------------------

int l_device_battery(lua_State* L) {
  lua_pushinteger(L, powerManager.getBatteryPercentage());
  return 1;
}

// Sleeps in short slices so Back can still abort a long sleep.
int l_device_sleep(lua_State* L) {
  const lua_Integer ms = luaL_checkinteger(L, 1);
  const unsigned long end = millis() + (ms > 0 ? static_cast<unsigned long>(ms) : 0);
  while (static_cast<long>(end - millis()) > 0) {
    if (ScriptEngine::pump(L)) return luaL_error(L, "aborted");
    delay(10);
  }
  return 0;
}

int l_device_millis(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(millis()));
  return 1;
}
int l_device_mac(lua_State* L) {
  lua_pushstring(L, WiFi.macAddress().c_str());
  return 1;
}
int l_device_version(lua_State* L) {
  lua_pushstring(L, CROSSPOINT_VERSION);
  return 1;
}

// device.mem() -> used, peak, limit (bytes of Lua heap). Lets a script — or the
// host simulator, which mirrors this binding — see how close it is running to
// the budget. Safe to call even at the ceiling: pushing three integers onto an
// already-sized stack cannot allocate.
int l_device_mem(lua_State* L) {
  const ScriptEngine* engine = ctx(L)->engine;
  lua_pushinteger(L, engine ? static_cast<lua_Integer>(engine->memUsed()) : 0);
  lua_pushinteger(L, engine ? static_cast<lua_Integer>(engine->memPeak()) : 0);
  lua_pushinteger(L, engine ? static_cast<lua_Integer>(engine->memLimit()) : 0);
  // Free heap as the engine saw it at launch — the input the limit was derived
  // from. Without it a script cannot tell a tight heap from a policy clamp.
  lua_pushinteger(L, engine ? static_cast<lua_Integer>(engine->freeHeapAtStart()) : 0);
  return 4;
}

// Keep only the most recent lines. This buffer is not displayed anywhere, and
// it lives on the device heap OUTSIDE the Lua memory cap — so an unbounded one
// let a print()-in-a-loop script eat the heap the reader needs, without ever
// tripping the script's own budget. The serial log below keeps the full record.
constexpr size_t kConsoleMaxLines = 64;

void appendConsole(lua_State* L, const std::string& line) {
  if (auto* console = ctx(L)->console) {
    if (console->size() >= kConsoleMaxLines) {
      console->erase(console->begin());
    }
    console->push_back(line);
  }
  LOG_INF("LUA", "%s", line.c_str());
}

int l_device_log(lua_State* L) {
  appendConsole(L, luaL_checkstring(L, 1));
  return 0;
}

// Clean quit from a script (e.g. after an "Exit?" confirm). Marks the run
// aborted so ScriptRunActivity shows the Stopped screen, same as the VM hook.
int l_device_exit(lua_State* L) {
  ctx(L)->aborted = true;
  return luaL_error(L, "aborted");
}

// print(...) -> join args with tabs, route to console + serial.
int l_print(lua_State* L) {
  const int n = lua_gettop(L);
  std::string line;
  for (int i = 1; i <= n; i++) {
    size_t len = 0;
    const char* s = luaL_tolstring(L, i, &len);  // uses __tostring
    if (i > 1) line += '\t';
    line.append(s, len);
    lua_pop(L, 1);  // luaL_tolstring pushes a copy
  }
  appendConsole(L, line);
  return 0;
}

// ---- require ---------------------------------------------------------------

// Minimal sandboxed require: searches /scripts/engine/ (the shared game engine)
// then /scripts/ (a game's own data modules), preferring a precompiled .luac
// over .lua within each. Dots in the name map to directories; results are cached
// per VM. This is the only way scripts can share code — dofile/loadfile/load
// stay out.
//
// Accepting bytecode is a real widening: lundump does not fully validate what it
// reads, so a corrupt .luac can misbehave where a corrupt .lua would only be a
// syntax error. The card's contents are already as trusted as the script itself
// — runFile has always loaded the main chunk with mode "bt" — and refusing it
// here would only mean modules could not be compiled while their caller could.
int l_require(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  for (const char* p = name; *p; ++p) {
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
    if (!ok) return luaL_error(L, "invalid module name '%s'", name);
  }

  luaL_getsubtable(L, LUA_REGISTRYINDEX, "scripts.loaded");
  lua_getfield(L, -1, name);
  if (!lua_isnil(L, -1)) return 1;  // cached
  lua_pop(L, 1);

  std::string rel;
  for (const char* p = name; *p; ++p) rel += (*p == '.') ? '/' : *p;

  // Bytecode first in each root. A card holding both is holding source to read
  // and bytecode to run, and the compiled one is what the build produced — so
  // it wins, rather than leaving which file ran depending on directory order.
  static const char* const kRoots[] = {"/scripts/engine/", "/scripts/"};
  std::string path;
  for (const char* root : kRoots) {
    const std::string base = std::string(root) + rel;
    if (Storage.exists((base + ".luac").c_str())) {
      path = base + ".luac";
      break;
    }
    if (Storage.exists((base + ".lua").c_str())) {
      path = base + ".lua";
      break;
    }
  }
  if (path.empty()) {
    return luaL_error(L,
                      "module '%s' not found — is /scripts/engine on the card? (looked for %s.luac and %s.lua in "
                      "/scripts/engine/ and /scripts/)",
                      name, rel.c_str(), rel.c_str());
  }
  bool truncated = false;
  size_t fileSize = 0;
  const String src = Storage.readFile(path.c_str(), &truncated, &fileSize);
  if (truncated) {
    return luaL_error(L, "module '%s': %s", name, scriptbindings::tooLargeMessage(path.c_str(), fileSize).c_str());
  }
  // Mode "bt": readFile is binary-safe (String::concat(char) forwards to the
  // length-taking overload, which memcpy's and sets an explicit length, so the
  // NULs bytecode is full of survive), and luaL_loadbufferx is given that
  // length rather than relying on termination.
  const std::string chunkName = "@" + path;
  if (luaL_loadbufferx(L, src.c_str(), src.length(), chunkName.c_str(), "bt") != LUA_OK) {
    return lua_error(L);
  }
  // Modules are where the debug weight actually is — a game's own file is small
  // next to what it requires. Stripping only the main chunk would leave most of
  // the saving on the table.
  if (SETTINGS.scriptStripDebug) luaStripDebugInfo(L, -1);
  lua_call(L, 0, 1);
  if (lua_isnil(L, -1)) {  // module returned nothing: cache `true` like stock Lua
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, name);
  return 1;
}

// ---- registration --------------------------------------------------------

const luaL_Reg kScreen[] = {{"clear", l_screen_clear},   {"text", l_screen_text},     {"line", l_screen_line},
                            {"rect", l_screen_rect},     {"pixel", l_screen_pixel},   {"image", l_screen_image},
                            {"save", l_screen_save},     {"width", l_screen_width},   {"height", l_screen_height},
                            {"invert", l_screen_invert}, {"update", l_screen_update}, {nullptr, nullptr}};

const luaL_Reg kInput[] = {
    {"wait", l_input_wait}, {"poll", l_input_poll}, {"down", l_input_down}, {nullptr, nullptr}};

const luaL_Reg kFs[] = {{"read", l_fs_read},     {"readRange", l_fs_readrange}, {"write", l_fs_write},
                        {"append", l_fs_append}, {"remove", l_fs_remove},       {"mkdir", l_fs_mkdir},
                        {"exists", l_fs_exists}, {"list", l_fs_list},           {nullptr, nullptr}};

const luaL_Reg kHttp[] = {{"get", l_http_get}, {nullptr, nullptr}};

const luaL_Reg kDevice[] = {{"battery", l_device_battery}, {"sleep", l_device_sleep},     {"millis", l_device_millis},
                            {"mac", l_device_mac},         {"version", l_device_version}, {"log", l_device_log},
                            {"exit", l_device_exit},       {"mem", l_device_mem},         {nullptr, nullptr}};

void registerModule(lua_State* L, const char* name, const luaL_Reg* funcs) {
  luaL_newlib(L, funcs);
  lua_setglobal(L, name);
}

}  // namespace

namespace scriptbindings {

std::string tooLargeMessage(const char* path, const size_t fileSize) {
  char buf[192];
  if (fileSize > Storage.maxReadFileBytes()) {
    snprintf(buf, sizeof(buf), "%s is %u bytes; the SD reader stops at %u. Split it into require()d modules.", path,
             static_cast<unsigned>(fileSize), static_cast<unsigned>(Storage.maxReadFileBytes()));
  } else {
    // Short of the cap but still short of the file: the heap could not hold it.
    snprintf(buf, sizeof(buf), "%s could not be read whole — not enough free heap for %u bytes.", path,
             static_cast<unsigned>(fileSize));
  }
  return buf;
}

void registerAll(lua_State* L) {
  registerModule(L, "screen", kScreen);
  registerModule(L, "input", kInput);
  registerModule(L, "fs", kFs);
  registerModule(L, "http", kHttp);
  registerModule(L, "device", kDevice);

  lua_pushcfunction(L, l_print);
  lua_setglobal(L, "print");
  lua_pushcfunction(L, l_require);
  lua_setglobal(L, "require");
}

}  // namespace scriptbindings
