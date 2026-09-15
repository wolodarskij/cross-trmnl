#pragma once

// BLE keyboard layouts: HID usage -> character tables for the languages a
// paired keyboard may be labelled in.
//
// The FreeInk host hands us the raw HID usage id + modifier byte of every key
// (KeyEvent::keycode / mods). Its own `ch` field is US-only, so text entry
// resolves the character here instead, against the layout the user picked in
// Settings -> System -> Bluetooth -> Keyboard layout.
//
// Adding a layout:
//   1. Add a `constexpr Key kXx[]` table in BleKeyboardLayouts.cpp listing the
//      keys that differ from its parent (or from US when parent is nullptr).
//      Use the hid:: names for usages and u'' literals for characters; a 0
//      column means "no character" (dead key, unused AltGr level, ...).
//   2. Add a `constexpr Layout kXxLayout` with a stable id (persisted in
//      settings.json, so never rename or reuse one), a name key added next to
//      the other layout names in lib/I18n/translations/english.yaml, and an
//      optional parent, then append its address to kLayouts[] (picker order).
//
// RAM: every table is `constexpr` at namespace scope, so it lives in the
// firmware's .rodata section, which on the ESP32-C3 is flash mapped through the
// cache — nothing is copied into DRAM at boot, and a layout that is never
// selected is never even read. Lookups walk the active table (and its parent
// chain) by pointer; no layout is ever materialised in RAM.

#include <I18n.h>

#include <cstdint>

namespace blelayout {

// USB HID keyboard/keypad usage ids (Usage Page 0x07) used by the tables.
namespace hid {
constexpr uint8_t A = 0x04, B = 0x05, C = 0x06, D = 0x07, E = 0x08, F = 0x09, G = 0x0A, H = 0x0B, I = 0x0C, J = 0x0D,
                  K = 0x0E, L = 0x0F, M = 0x10, N = 0x11, O = 0x12, P = 0x13, Q = 0x14, R = 0x15, S = 0x16, T = 0x17,
                  U = 0x18, V = 0x19, W = 0x1A, X = 0x1B, Y = 0x1C, Z = 0x1D;
constexpr uint8_t N1 = 0x1E, N2 = 0x1F, N3 = 0x20, N4 = 0x21, N5 = 0x22, N6 = 0x23, N7 = 0x24, N8 = 0x25, N9 = 0x26,
                  N0 = 0x27;
constexpr uint8_t SPACE = 0x2C;
constexpr uint8_t MINUS = 0x2D;       // US: - _
constexpr uint8_t EQUAL = 0x2E;       // US: = +
constexpr uint8_t LBRACKET = 0x2F;    // US: [ {
constexpr uint8_t RBRACKET = 0x30;    // US: ] }
constexpr uint8_t BACKSLASH = 0x31;   // US ANSI: \ |  (key above Enter)
constexpr uint8_t NONUS_HASH = 0x32;  // ISO: the key left of Enter (# ~ on UK boards)
constexpr uint8_t SEMICOLON = 0x33;   // US: ; :
constexpr uint8_t QUOTE = 0x34;       // US: ' "
constexpr uint8_t GRAVE = 0x35;       // US: ` ~
constexpr uint8_t COMMA = 0x36;       // US: , <
constexpr uint8_t DOT = 0x37;         // US: . >
constexpr uint8_t SLASH = 0x38;       // US: / ?
constexpr uint8_t CAPS_LOCK = 0x39;
constexpr uint8_t KP_SLASH = 0x54, KP_STAR = 0x55, KP_MINUS = 0x56, KP_PLUS = 0x57;
constexpr uint8_t KP_1 = 0x59, KP_2 = 0x5A, KP_3 = 0x5B, KP_4 = 0x5C, KP_5 = 0x5D, KP_6 = 0x5E, KP_7 = 0x5F,
                  KP_8 = 0x60, KP_9 = 0x61, KP_0 = 0x62, KP_DOT = 0x63;
constexpr uint8_t NONUS_BSLASH = 0x64;  // ISO: the extra key between left Shift and Z (< > on most boards)
constexpr uint8_t KP_COMMA = 0x85;      // Brazilian ABNT2 keypad comma
constexpr uint8_t INTL1 = 0x87;         // Brazilian ABNT2 / ? key (right of right Shift)
}  // namespace hid

// HID modifier bits (byte 0 of a keyboard report).
namespace mod {
constexpr uint8_t LCTRL = 0x01, LSHIFT = 0x02, LALT = 0x04, LGUI = 0x08, RCTRL = 0x10, RSHIFT = 0x20, RALT = 0x40,
                  RGUI = 0x80;
}  // namespace mod

// One physical key: the character on each modifier level. 0 = no character.
// Trailing columns may be omitted in the tables (aggregate init zero-fills).
struct Key {
  uint8_t usage;
  char16_t base;
  char16_t shift;
  char16_t altgr;       // AltGr (right Alt, or Ctrl+Alt)
  char16_t shiftAltgr;  // Shift + AltGr
};

struct Layout {
  const char* id;   // stable, persisted in settings.json ("us", "de", ...)
  StrId nameId;     // label shown in the picker
  const Key* keys;  // keys that differ from `parent` (or from US)
  uint16_t keyCount;
  const Layout* parent;  // consulted for keys not listed here; nullptr = US
};

// All registered layouts, in picker order.
uint8_t count();
const Layout& at(uint8_t index);
// Resolve a persisted id. Returns nullptr for unknown ids.
const Layout* find(const char* id);
// Index of a registered layout in picker order (0 when not registered).
uint8_t indexOf(const Layout& layout);
// The US layout, root of every parent chain.
const Layout& fallback();

// Character for a HID usage under the given modifier byte and Caps Lock state,
// or 0 when the key produces no text on this level. Keys missing from `layout`
// fall through its parent chain and finally to US, so a table only has to list
// what differs. Caps Lock inverts Shift for keys whose base/shift pair is a
// lower/upper-case letter pair (ASCII, Latin-1, Latin Extended-A, Greek,
// Cyrillic) and is ignored for everything else, like a real keyboard.
char16_t lookup(const Layout& layout, uint8_t usage, uint8_t mods, bool capsLock);

}  // namespace blelayout
