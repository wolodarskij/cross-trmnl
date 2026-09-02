#pragma once

// CrossPoint <-> FreeInk BLE HID host glue.
//
// Thin, capability-safe helpers around freeink::BleKeyboardHost (the `BleHid`
// singleton). When FREEINK_CAP_BLE_HID_HOST is compiled out the SDK links stubs,
// so every call here is still valid and simply no-ops / returns false — callers
// need no #ifdefs.
//
// The (kind, value) pair produced by encodeKey() is the stable identity stored in
// CrossPointSettings::bleKeyMap. Page-turner remotes emit "special" keys
// (PageUp/PageDown/arrows); plain keyboards emit usage codes. We deliberately
// ignore modifiers and the printable char for matching (page turners don't use
// modifiers), keeping the persisted entry a trivial two-byte comparison.

#include <BleKeyboardHost.h>

#include <cstdint>

class GfxRenderer;
class MappedInputManager;

namespace bleinput {

// Advertised central name shown to peripherals during pairing.
inline constexpr const char* kHostName = "CrossPoint";

// Heap floor for starting the NimBLE stack in a READER context (measured begin()
// cost: ~52-57 KB).
//
// This must clear NimBLE's own cost *plus* the reader's background-build floor
// (EpubReaderActivity::BUILD_TICK_MIN_FREE_HEAP), or the two gates oscillate:
// starting at a floor of ~56 KB leaves almost nothing free, the next build tick
// sees a starved heap and sheds BLE, freeing ~52 KB, which puts the heap back
// over the start floor — and so on, every loop. Upstream can afford the lower
// floor because its reader lends the 48 KB framebuffer to section builds; that
// loan mechanism is deliberately not ported here (see FEATURES.md), so the
// headroom has to come from this floor instead.
inline constexpr size_t kStartMinFreeHeap = 84 * 1024;

// Lower floor for the Bluetooth settings screen, where the user has explicitly asked
// for BLE right now (scanning/pairing is dead without the stack). No page renders or
// section builds run there, so the reader-sized reserve above doesn't apply — only
// NimBLE's own ~57 KB plus working margin.
inline constexpr size_t kStartMinFreeHeapExplicit = 70 * 1024;

// Start the BLE HID host (idempotent). Returns false if BLE is compiled out or
// NimBLE init failed. Safe to call repeatedly.
bool ensureStarted();
bool startInProgress();

// Drop the active link (e.g. before deep sleep or when the user disables BT).
void stop();

// Encode a decoded key event into the stable (kind, value) identity used by the
// settings map. kind: 0 = SpecialKey, 1 = HID usage. Returns false when the event
// carries no usable identity (no special key and no usage code).
bool encodeKey(const freeink::KeyEvent& ev, uint8_t& kind, uint8_t& value);

// Human-readable name for a stored (kind, value) identity, for the mapping UI.
// Writes a null-terminated string into out (e.g. "Page Down", "Key 0x4B").
void describeKey(uint8_t kind, uint8_t value, char* out, size_t outLen);

}  // namespace bleinput
