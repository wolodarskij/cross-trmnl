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

#include <cstring>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

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
  if (fill) {
    gfx(L).fillRect(x, y, w, h, true);
  } else {
    gfx(L).drawRect(x, y, w, h, true);
  }
  return 0;
}

int l_screen_pixel(lua_State* L) {
  const int x = static_cast<int>(luaL_checkinteger(L, 1));
  const int y = static_cast<int>(luaL_checkinteger(L, 2));
  const bool on = lua_isnone(L, 3) ? true : lua_toboolean(L, 3);
  gfx(L).drawPixel(x, y, on);
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

const char* pressedButtonName(MappedInputManager& in, bool& isBack) {
  isBack = false;
  struct Map {
    Button b;
    const char* n;
  };
  static const Map kMap[] = {{Button::Confirm, "confirm"}, {Button::Left, "left"}, {Button::Right, "right"},
                             {Button::Up, "up"},           {Button::Down, "down"}, {Button::Power, "power"}};
  for (const auto& m : kMap) {
    if (in.wasPressed(m.b)) return m.n;
  }
  if (in.wasPressed(Button::Back)) {
    isBack = true;
    return "back";
  }
  return nullptr;
}

// Blocks until a button is pressed. Back aborts the script (universal quit),
// so scripts use the other buttons for interaction.
int l_input_wait(lua_State* L) {
  MappedInputManager& in = *ctx(L)->input;
  for (;;) {
    in.update();
    bool isBack = false;
    const char* name = pressedButtonName(in, isBack);
    if (isBack) return luaL_error(L, "aborted");
    if (name) {
      lua_pushstring(L, name);
      return 1;
    }
    delay(15);
  }
}

// Non-blocking: returns a button name or nil. Back still aborts.
int l_input_poll(lua_State* L) {
  MappedInputManager& in = *ctx(L)->input;
  in.update();
  bool isBack = false;
  const char* name = pressedButtonName(in, isBack);
  if (isBack) return luaL_error(L, "aborted");
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
  const String content = Storage.readFile(path);
  lua_pushlstring(L, content.c_str(), content.length());
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
  String combined = Storage.exists(path.c_str()) ? Storage.readFile(path.c_str()) : String();
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

void appendConsole(lua_State* L, const std::string& line) {
  if (ctx(L)->console) ctx(L)->console->push_back(line);
  LOG_INF("LUA", "%s", line.c_str());
}

int l_device_log(lua_State* L) {
  appendConsole(L, luaL_checkstring(L, 1));
  return 0;
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

// ---- registration --------------------------------------------------------

const luaL_Reg kScreen[] = {{"clear", l_screen_clear},   {"text", l_screen_text},     {"line", l_screen_line},
                            {"rect", l_screen_rect},     {"pixel", l_screen_pixel},   {"image", l_screen_image},
                            {"width", l_screen_width},   {"height", l_screen_height}, {"invert", l_screen_invert},
                            {"update", l_screen_update}, {nullptr, nullptr}};

const luaL_Reg kInput[] = {
    {"wait", l_input_wait}, {"poll", l_input_poll}, {"down", l_input_down}, {nullptr, nullptr}};

const luaL_Reg kFs[] = {{"read", l_fs_read},     {"write", l_fs_write},   {"append", l_fs_append},
                        {"remove", l_fs_remove}, {"mkdir", l_fs_mkdir},   {"exists", l_fs_exists},
                        {"list", l_fs_list},     {nullptr, nullptr}};

const luaL_Reg kHttp[] = {{"get", l_http_get}, {nullptr, nullptr}};

const luaL_Reg kDevice[] = {{"battery", l_device_battery}, {"sleep", l_device_sleep},     {"millis", l_device_millis},
                            {"mac", l_device_mac},         {"version", l_device_version}, {"log", l_device_log},
                            {nullptr, nullptr}};

void registerModule(lua_State* L, const char* name, const luaL_Reg* funcs) {
  luaL_newlib(L, funcs);
  lua_setglobal(L, name);
}

}  // namespace

namespace scriptbindings {

void registerAll(lua_State* L) {
  registerModule(L, "screen", kScreen);
  registerModule(L, "input", kInput);
  registerModule(L, "fs", kFs);
  registerModule(L, "http", kHttp);
  registerModule(L, "device", kDevice);

  lua_pushcfunction(L, l_print);
  lua_setglobal(L, "print");
}

}  // namespace scriptbindings
