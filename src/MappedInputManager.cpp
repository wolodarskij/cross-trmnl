#include "MappedInputManager.h"

#include <GfxRenderer.h>

#include "BleInput.h"
#include "CrossPointSettings.h"

bool MappedInputManager::isNavDirectionSwapped() const {
  // Key the swap on the orientation the screen is *actually* rendered at, not the persisted reader
  // setting. The reader (and its modal menus) render rotated, so navigation/labels flip there; the
  // home and settings UI render in portrait, so they never flip even when a rotated reader is configured.
  const auto orientation = renderer.getOrientation();
  return SETTINGS.frontButtonFollowOrientation &&
         (orientation == GfxRenderer::PortraitInverted || orientation == GfxRenderer::LandscapeCounterClockwise);
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = SETTINGS.sideButtonLayout;

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(HalGPIO::BTN_UP);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::NavNext:
      // Logical "next item" navigation: side Down + front Right, with the control axis flipped in
      // INVERTED / LANDSCAPE_CCW (frontButtonFollowOrientation) so it matches the rotated hint labels.
      return isNavDirectionSwapped() ? (mapButton(Button::Up, fn) || mapButton(Button::Left, fn))
                                     : (mapButton(Button::Down, fn) || mapButton(Button::Right, fn));
    case Button::NavPrevious:
      // Logical "previous item" navigation: side Up + front Left, axis-flipped in the same orientations.
      return isNavDirectionSwapped() ? (mapButton(Button::Down, fn) || mapButton(Button::Right, fn))
                                     : (mapButton(Button::Up, fn) || mapButton(Button::Left, fn));
  }

  return false;
}

bool MappedInputManager::bleEdge(const bool* arr, const Button button) const {
  // Mirror mapButton()'s composite navigation handling so a BLE key bound to a
  // physical direction also satisfies the derived NavNext / NavPrevious logical
  // buttons (used by list navigation), respecting the orientation axis flip.
  switch (button) {
    case Button::NavNext:
      return isNavDirectionSwapped() ? (arr[(int)Button::Up] || arr[(int)Button::Left])
                                     : (arr[(int)Button::Down] || arr[(int)Button::Right]);
    case Button::NavPrevious:
      return isNavDirectionSwapped() ? (arr[(int)Button::Down] || arr[(int)Button::Right])
                                     : (arr[(int)Button::Up] || arr[(int)Button::Left]);
    default:
      return arr[(int)button];
  }
}

bool MappedInputManager::wasPressed(const Button button) const {
  return mapButton(button, &HalGPIO::wasPressed) || bleEdge(blePressEdge, button);
}

bool MappedInputManager::wasReleased(const Button button) const {
  return mapButton(button, &HalGPIO::wasReleased) || bleEdge(bleReleaseEdge, button);
}

bool MappedInputManager::isPressed(const Button button) const {
  // A BLE tap is momentary: report "pressed" only on the press-edge frame.
  return mapButton(button, &HalGPIO::isPressed) || bleEdge(blePressEdge, button);
}

void MappedInputManager::setBleCaptureMode(const bool on) {
  bleCaptureMode = on;
  bleHasCaptured = false;
  if (on) {
    // Clear any stale overlay so a held remote key doesn't leak into the UI.
    for (uint8_t i = 0; i < kButtonCount; i++) {
      blePressEdge[i] = false;
      bleReleaseEdge[i] = false;
    }
  }
}

bool MappedInputManager::takeCapturedBleKey(uint8_t& kind, uint8_t& value) {
  if (!bleHasCaptured) return false;
  kind = bleCapturedKind;
  value = bleCapturedValue;
  bleHasCaptured = false;
  return true;
}

void MappedInputManager::setBleTextSink(const bool on) {
  bleTextSink = on;
  // Drop anything queued under the previous mode so a key pressed on the way in
  // (usually the Confirm/Enter that opened the text UI) doesn't replay into it.
  bleTextHead = bleTextTail = 0;
  if (on) {
    // Also clear the button overlay: a press edge latched this frame must not
    // keep firing logical buttons under the text UI.
    for (uint8_t i = 0; i < kButtonCount; i++) {
      blePressEdge[i] = false;
      bleReleaseEdge[i] = false;
    }
  }
}

bool MappedInputManager::popBleTextKey(freeink::KeyEvent& out) {
  if (bleTextTail == bleTextHead) return false;
  out = bleTextRing[bleTextTail];
  bleTextTail = static_cast<uint8_t>((bleTextTail + 1) % kBleTextRingLen);
  return true;
}

void MappedInputManager::pollBle() {
  bleActivityThisFrame = false;
  // Age last frame's press edges into this frame's release edges (the FreeInk host
  // surfaces presses + synthetic repeats but never releases), then clear presses. A
  // pending release also counts as BLE activity this frame so getHeldTime() reports
  // zero on the release frame too (page-turn handlers often fire on release).
  for (uint8_t i = 0; i < kButtonCount; i++) {
    bleReleaseEdge[i] = blePressEdge[i];
    blePressEdge[i] = false;
    if (bleReleaseEdge[i]) bleActivityThisFrame = true;
  }

  freeink::KeyEvent ev;
  while (BleHid.popKey(ev)) {
    uint8_t kind = 0xFF;
    uint8_t value = 0;
    if (!bleinput::encodeKey(ev, kind, value)) continue;

    if (bleCaptureMode) {
      bleCapturedKind = kind;
      bleCapturedValue = value;
      bleHasCaptured = true;
      continue;
    }

    if (bleTextSink) {
      // Full-fidelity delivery for the active text UI (printable char + special).
      // Drop-oldest on overflow so a typing burst degrades to lost oldest keys
      // rather than a stuck ring. Still counts as activity for the sleep timer.
      const uint8_t next = static_cast<uint8_t>((bleTextHead + 1) % kBleTextRingLen);
      if (next == bleTextTail) bleTextTail = static_cast<uint8_t>((bleTextTail + 1) % kBleTextRingLen);
      bleTextRing[bleTextHead] = ev;
      bleTextHead = next;
      bleActivityThisFrame = true;
      continue;
    }

    // Resolve the key identity against the persisted mapping table.
    bool mapped = false;
    for (const auto& e : SETTINGS.bleKeyMap) {
      if (e.button == 0xFF || e.keyKind != kind || e.keyValue != value) continue;
      if (e.button < kButtonCount) {
        blePressEdge[e.button] = true;
        bleActivityThisFrame = true;
      }
      mapped = true;
      break;
    }
    if (mapped) continue;

    // Default keymap for unmapped special keys, so a plain keyboard navigates every
    // menu out of the box (arrows/Enter/Escape/PageUp/PageDown). A user mapping for
    // the same key always wins (checked above); page-turner remotes that emit these
    // usages therefore also work before any mapping session.
    if (kind == 0) {
      Button b;
      switch (static_cast<freeink::SpecialKey>(value)) {
        case freeink::SpecialKey::Up:
          b = Button::Up;
          break;
        case freeink::SpecialKey::Down:
          b = Button::Down;
          break;
        case freeink::SpecialKey::Left:
          b = Button::Left;
          break;
        case freeink::SpecialKey::Right:
          b = Button::Right;
          break;
        case freeink::SpecialKey::Enter:
          b = Button::Confirm;
          break;
        case freeink::SpecialKey::Escape:
        case freeink::SpecialKey::Backspace:
          b = Button::Back;
          break;
        case freeink::SpecialKey::PageUp:
          b = Button::PageBack;
          break;
        case freeink::SpecialKey::PageDown:
          b = Button::PageForward;
          break;
        default:
          continue;
      }
      blePressEdge[static_cast<uint8_t>(b)] = true;
      bleActivityThisFrame = true;
    }
  }
}

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const {
  // A BLE-mapped key is a momentary tap with no physical hold (we don't model BLE
  // press-and-hold). gpio.getHeldTime() returns the *last physical* button's hold
  // duration, which is stale — if a BLE edge drove input this frame, reporting that
  // stale value makes a tap look like a long-press (e.g. page tap -> chapter skip).
  // Report zero in that case so BLE taps are always treated as short presses.
  if (bleActivityThisFrame) return 0;
  return gpio.getHeldTime();
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Swap previous/next labels to match the page turn direction swap in INVERTED and LANDSCAPE_CCW.
  const bool swapLabels = isNavDirectionSwapped();
  const char* leftLabel = swapLabels ? next : previous;
  const char* rightLabel = swapLabels ? previous : next;

  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return leftLabel;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return rightLabel;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
