#include "BleInput.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace bleinput {

namespace {
volatile bool g_startInProgress = false;
// Caps Lock state of the connected keyboard. HID reports carry Shift/Ctrl/Alt/GUI
// as modifier bits but Caps Lock only as a key press, so the host toggles it
// itself. Reset when the stack goes down: a newly paired keyboard starts off.
bool g_capsLock = false;
}  // namespace

// NimBLE controller init/deinit hang (interrupt WDT) if run at the 10 MHz low-power
// frequency, so force normal CPU speed around both. Centralized here so every caller
// (boot restore, settings toggle, reader toggle, sleep) is covered automatically.
bool ensureStarted() {
  g_startInProgress = true;
  HalPowerManager::Lock powerLock;
  const bool ok = BleHid.begin(kHostName);
  g_startInProgress = false;
  return ok;
}

bool startInProgress() { return g_startInProgress; }

// Full teardown (NimBLE deinit), not just a link drop, so the BLE stack's RAM is
// returned to the heap — otherwise memory-hungry work like EPUB inflate can't
// allocate even after the user turns Bluetooth off.
void stop() {
  HalPowerManager::Lock powerLock;
  BleHid.end();
  g_capsLock = false;
}

bool encodeKey(const freeink::KeyEvent& ev, uint8_t& kind, uint8_t& value) {
  if (ev.special != freeink::SpecialKey::None) {
    kind = 0;
    value = static_cast<uint8_t>(ev.special);
    return true;
  }
  if (ev.keycode != 0) {
    kind = 1;
    value = ev.keycode;
    return true;
  }
  return false;
}

namespace {
const char* specialName(uint8_t value) {
  switch (static_cast<freeink::SpecialKey>(value)) {
    case freeink::SpecialKey::Enter:
      return "Enter";
    case freeink::SpecialKey::Backspace:
      return "Backspace";
    case freeink::SpecialKey::Tab:
      return "Tab";
    case freeink::SpecialKey::Escape:
      return "Escape";
    case freeink::SpecialKey::Delete:
      return "Delete";
    case freeink::SpecialKey::Left:
      return "Left";
    case freeink::SpecialKey::Right:
      return "Right";
    case freeink::SpecialKey::Up:
      return "Up";
    case freeink::SpecialKey::Down:
      return "Down";
    case freeink::SpecialKey::Home:
      return "Home";
    case freeink::SpecialKey::End:
      return "End";
    case freeink::SpecialKey::PageUp:
      return "Page Up";
    case freeink::SpecialKey::PageDown:
      return "Page Down";
    default:
      return nullptr;
  }
}
}  // namespace

void observeKey(const freeink::KeyEvent& ev) {
  if (ev.keycode == blelayout::hid::CAPS_LOCK) g_capsLock = !g_capsLock;
}

const blelayout::Layout& activeLayout() {
  // Re-resolved only when the persisted id changes; a strcmp of a <8-byte id
  // per keystroke is the whole cost of staying in sync with the setting.
  static const blelayout::Layout* cached = nullptr;
  static char cachedId[sizeof(SETTINGS.bleKeyboardLayout)] = "";
  if (!cached || strcmp(cachedId, SETTINGS.bleKeyboardLayout) != 0) {
    cached = blelayout::find(SETTINGS.bleKeyboardLayout);
    if (!cached) cached = &blelayout::fallback();
    strncpy(cachedId, SETTINGS.bleKeyboardLayout, sizeof(cachedId) - 1);
    cachedId[sizeof(cachedId) - 1] = '\0';
  }
  return *cached;
}

bool keyText(const freeink::KeyEvent& ev, char* out, size_t outLen) {
  if (!out || outLen < 5) return false;
  out[0] = '\0';
  if (ev.special != freeink::SpecialKey::None || ev.keycode == 0) return false;

  // Ctrl / GUI / left Alt held: a shortcut chord, never text. Right Alt is
  // AltGr (a character level on most non-US layouts); Ctrl+Alt is the Windows
  // AltGr emulation that some keyboards send instead of the RALT bit.
  using namespace blelayout::mod;
  const uint8_t chordBits = ev.mods & (LCTRL | RCTRL | LALT | LGUI | RGUI);
  if ((ev.mods & RALT) != 0) {
    if (chordBits != 0) return false;
  } else if (chordBits != 0 && chordBits != (LCTRL | LALT)) {
    return false;
  }

  const char16_t cp = blelayout::lookup(activeLayout(), ev.keycode, ev.mods, g_capsLock);
  if (cp == 0) return false;

  // Layout tables hold BMP code points, so 1-3 UTF-8 bytes.
  size_t n = 0;
  if (cp < 0x80) {
    out[n++] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    out[n++] = static_cast<char>(0xC0 | (cp >> 6));
    out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out[n++] = static_cast<char>(0xE0 | (cp >> 12));
    out[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  out[n] = '\0';
  return true;
}

void describeKey(uint8_t kind, uint8_t value, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  if (kind == 0) {
    const char* name = specialName(value);
    if (name) {
      strncpy(out, name, outLen - 1);
      out[outLen - 1] = '\0';
      return;
    }
  }
  // Printable ASCII usage handled as a generic key code; show the raw value.
  snprintf(out, outLen, "Key 0x%02X", static_cast<unsigned>(value));
}

}  // namespace bleinput
