#include "BluetoothSettingsActivity.h"

#include <BleKeyboardHost.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "BleButtonMapActivity.h"
#include "BleInput.h"
#include "BleKeyboardLayouts.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kBannerMs = 2000;
constexpr uint32_t kScanMs = 8000;
constexpr unsigned long kForgetHoldMs = 1200;  // hold Confirm this long in the Paired view to forget
}  // namespace

void BluetoothSettingsActivity::onEnter() {
  Activity::onEnter();
  view = View::Menu;
  menuIndex = 0;
  rebuildMenuRows();
  requestUpdate();
}

void BluetoothSettingsActivity::onExit() {
  if (BleHid.isScanning()) BleHid.stopScan();
  Activity::onExit();
}

void BluetoothSettingsActivity::setBanner(const char* text) {
  banner = text ? text : "";
  bannerUntil = millis() + kBannerMs;
}

void BluetoothSettingsActivity::rebuildMenuRows() {
  menuRows.clear();
  menuRows.reserve(8);
  menuRows.push_back({Action::ToggleBt, StrId::STR_BLUETOOTH});
  if (SETTINGS.bluetoothEnabled) {
    menuRows.push_back({Action::Scan, StrId::STR_BT_SCAN_PAIR});
    if (BleHid.isConnected()) menuRows.push_back({Action::Disconnect, StrId::STR_BT_DISCONNECT});
    menuRows.push_back({Action::PairedDevices, StrId::STR_BT_PAIRED_DEVICES});
    menuRows.push_back({Action::MapButtons, StrId::STR_BT_MAP_BUTTONS});
    menuRows.push_back({Action::KeyboardLayout, StrId::STR_BT_KEYBOARD_LAYOUT});
    menuRows.push_back({Action::HideOsk, StrId::STR_BT_HIDE_OSK});
  }
  if (menuIndex >= static_cast<int>(menuRows.size())) menuIndex = 0;
}

void BluetoothSettingsActivity::startScanView() {
  LOG_INF("BLEUI", "scan view: begin running=%d scanning=%d devices=%u paired=%u", BleHid.isRunning(),
          BleHid.isScanning(), BleHid.deviceCount(), BleHid.pairedCount());
  view = View::Scan;
  scanIndex = 0;
  awaitingConnect = false;
  lastLoggedScanState = false;
  lastLoggedDeviceCount = 0xFF;
  // The main-loop lifecycle owns steady-state start/stop, but a scan needs the stack
  // this instant — entering this screen can precede the lifecycle's next tick, or its
  // heap gate may have deferred the start. ensureStarted() is idempotent, and with
  // this screen on top the lifecycle keeps the stack up (keepsBluetoothAlive).
  if (!BleHid.isRunning() && !bleinput::ensureStarted()) {
    LOG_ERR("BLEUI", "scan: BLE start failed (heap=%u)", ESP.getFreeHeap());
  }
  BleHid.startScan(kScanMs);
  LOG_INF("BLEUI", "scan view: startScan requested scanning=%d devices=%u", BleHid.isScanning(), BleHid.deviceCount());
  requestUpdate();
}

void BluetoothSettingsActivity::selectLayout(int index) {
  if (index < 0 || index >= blelayout::count()) return;
  const char* id = blelayout::at(static_cast<uint8_t>(index)).id;
  if (strcmp(SETTINGS.bleKeyboardLayout, id) != 0) {
    strncpy(SETTINGS.bleKeyboardLayout, id, sizeof(SETTINGS.bleKeyboardLayout) - 1);
    SETTINGS.bleKeyboardLayout[sizeof(SETTINGS.bleKeyboardLayout) - 1] = '\0';
    SETTINGS.saveToFile();
  }
  view = View::Menu;
  requestUpdate();
}

void BluetoothSettingsActivity::handleMenuConfirm() {
  if (menuRows.empty()) return;
  const Action action = menuRows[menuIndex].action;
  switch (action) {
    case Action::ToggleBt:
      // Flip the preference only; the main-loop lifecycle check starts/stops the BLE
      // stack to match (and shows the "BT Connecting..." popup). Single owner.
      SETTINGS.bluetoothEnabled = SETTINGS.bluetoothEnabled ? 0 : 1;
      SETTINGS.saveToFile();
      rebuildMenuRows();
      requestUpdate();
      break;
    case Action::Scan:
      startScanView();
      break;
    case Action::Disconnect:
      BleHid.disconnect();
      setBanner(tr(STR_BT_NOT_CONNECTED));
      rebuildMenuRows();
      requestUpdate();
      break;
    case Action::PairedDevices:
      view = View::Paired;
      pairedIndex = 0;
      requestUpdate();
      break;
    case Action::MapButtons:
      startActivityForResult(std::make_unique<BleButtonMapActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               rebuildMenuRows();
                               requestUpdate();
                             });
      break;
    case Action::KeyboardLayout:
      view = View::Layout;
      layoutIndex = blelayout::indexOf(bleinput::activeLayout());
      requestUpdate();
      break;
    case Action::HideOsk:
      SETTINGS.bleHideOnScreenKeyboard = SETTINGS.bleHideOnScreenKeyboard ? 0 : 1;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
  }
}

void BluetoothSettingsActivity::loop() {
  // Clear an expired status banner.
  if (bannerUntil > 0 && millis() > bannerUntil) {
    banner.clear();
    bannerUntil = 0;
    requestUpdate();
  }

  // Watch for an async connect result (from either the scan list or the paired list).
  if (awaitingConnect) {
    char reason[48];
    if (BleHid.isConnected()) {
      awaitingConnect = false;
      BleHid.releaseScanResults();
      view = View::Menu;
      rebuildMenuRows();
      char buf[64];
      snprintf(buf, sizeof(buf), tr(STR_BT_CONNECTED_TO), BleHid.connectedName());
      setBanner(buf);
      requestUpdate();
    } else if (BleHid.takeConnectFailure(reason, sizeof(reason))) {
      awaitingConnect = false;
      setBanner(reason);
      requestUpdate();
    }
  }

  // Back returns to the menu from a sub-view, or leaves the screen from the menu.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      finish();
    } else {
      if (BleHid.isScanning()) BleHid.stopScan();
      view = View::Menu;
      rebuildMenuRows();
      requestUpdate();
    }
    return;
  }

  // Navigation within the active list. drawList() pages by itself, so a list
  // longer than the screen scrolls as the index moves past the last visible row.
  int count = 0;
  int* idx = &menuIndex;
  switch (view) {
    case View::Menu:
      count = static_cast<int>(menuRows.size());
      idx = &menuIndex;
      break;
    case View::Scan:
      count = BleHid.deviceCount();
      idx = &scanIndex;
      break;
    case View::Paired:
      count = BleHid.pairedCount();
      idx = &pairedIndex;
      break;
    case View::Layout:
      count = blelayout::count();
      idx = &layoutIndex;
      break;
  }
  buttonNavigator.onNext([this, count, idx] {
    if (count > 0) *idx = ButtonNavigator::nextIndex(*idx, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count, idx] {
    if (count > 0) *idx = ButtonNavigator::previousIndex(*idx, count);
    requestUpdate();
  });

  // Paired view: tap Confirm to connect, hold Confirm to forget. Uses release for
  // connect so a hold can fire forget without also connecting on the same press.
  if (view == View::Paired) {
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      if (!pairedActionTaken && mappedInput.getHeldTime() >= kForgetHoldMs && pairedIndex < BleHid.pairedCount()) {
        const auto& p = BleHid.paired(static_cast<uint8_t>(pairedIndex));
        BleHid.forget(p.addr);
        if (pairedIndex > 0) pairedIndex--;
        setBanner(tr(STR_FORGET_BUTTON));
        pairedActionTaken = true;
        rebuildMenuRows();
        requestUpdate();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!pairedActionTaken && !awaitingConnect && pairedIndex < BleHid.pairedCount()) {
        if (pairedIsConnected(pairedIndex)) {
          // Already linked: a second connect() to the same peer fails inside NimBLE
          // and surfaced as a spurious "Connection failed" banner. Just say so.
          char buf[64];
          snprintf(buf, sizeof(buf), tr(STR_BT_CONNECTED_TO), BleHid.connectedName());
          setBanner(buf);
        } else {
          const auto& p = BleHid.paired(static_cast<uint8_t>(pairedIndex));
          requestConnect(p.addr);
        }
        requestUpdate();
      }
      pairedActionTaken = false;
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (view == View::Menu) {
      handleMenuConfirm();
    } else if (view == View::Layout) {
      selectLayout(layoutIndex);
    } else if (view == View::Scan) {
      const int scanCount = BleHid.deviceCount();
      if (!awaitingConnect && scanCount == 0 && !BleHid.isScanning()) {
        LOG_INF("BLEUI", "scan view: restart scan requested");
        if (!BleHid.isRunning() && !bleinput::ensureStarted()) {
          LOG_ERR("BLEUI", "scan restart: BLE start failed (heap=%u)", ESP.getFreeHeap());
        }
        BleHid.startScan(kScanMs);
        LOG_INF("BLEUI", "scan view: restart scan state scanning=%d devices=%u", BleHid.isScanning(),
                BleHid.deviceCount());
        requestUpdate();
      } else if (!awaitingConnect && scanIndex < scanCount) {
        if (BleHid.isScanning()) BleHid.stopScan();
        const auto& d = BleHid.device(static_cast<uint8_t>(scanIndex));
        LOG_INF("BLEUI", "scan view: connect addr=%s name='%s' rssi=%d type=%u hid=%d conn=%d", d.addr, d.name, d.rssi,
                d.addrType, d.hid, d.connectable);
        requestConnect(d.addr);
        requestUpdate();
      }
    }
    return;
  }

  // The scan list changes as devices are discovered — keep repainting while active.
  if (view == View::Scan) {
    const bool scanning = BleHid.isScanning();
    const uint8_t deviceCount = BleHid.deviceCount();
    if (scanning != lastLoggedScanState || deviceCount != lastLoggedDeviceCount) {
      LOG_INF("BLEUI", "scan view: state scanning=%d devices=%u", scanning, deviceCount);
      lastLoggedScanState = scanning;
      lastLoggedDeviceCount = deviceCount;
    }
    if (scanning) requestUpdate();
  }
}

std::string BluetoothSettingsActivity::deviceLabel(int index) const {
  if (index >= BleHid.deviceCount()) return "";
  const auto& d = BleHid.device(static_cast<uint8_t>(index));
  return std::string(d.name);
}

std::string BluetoothSettingsActivity::pairedLabel(int index) const {
  if (index >= BleHid.pairedCount()) return "";
  const auto& p = BleHid.paired(static_cast<uint8_t>(index));
  return std::string(p.name);
}

bool BluetoothSettingsActivity::pairedIsConnected(int index) const {
  if (!BleHid.isConnected() || index >= BleHid.pairedCount()) return false;
  // The host exposes the live link by name only; it resolves that name from the
  // bond list at link-up, so a bonded device matches its own entry here.
  const auto& p = BleHid.paired(static_cast<uint8_t>(index));
  return p.name[0] != '\0' && strcmp(p.name, BleHid.connectedName()) == 0;
}

void BluetoothSettingsActivity::requestConnect(const char* addr) {
  // connect() refuses while another attempt is in flight (or BLE is down); it
  // reports no failure event in that case, so arming the watcher would leave the
  // view stuck on "Connecting..." for good.
  if (!BleHid.connect(addr)) {
    setBanner(BleHid.isConnecting() ? tr(STR_CONNECTING) : tr(STR_BT_NOT_CONNECTED));
    return;
  }
  awaitingConnect = true;
  setBanner(tr(STR_CONNECTING));
}

std::string BluetoothSettingsActivity::menuValue(int index) const {
  switch (menuRows[index].action) {
    case Action::ToggleBt:
      return SETTINGS.bluetoothEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case Action::HideOsk:
      return SETTINGS.bleHideOnScreenKeyboard ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case Action::KeyboardLayout:
      return I18N.get(bleinput::activeLayout().nameId);
    default:
      return "";
  }
}

void BluetoothSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* title = tr(STR_BLUETOOTH);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  // Sub-header: connection status, or the picker's own title in the Layout view.
  const char* status = view == View::Layout   ? tr(STR_BT_KEYBOARD_LAYOUT)
                       : BleHid.isConnected() ? BleHid.connectedName()
                                              : tr(STR_BT_NOT_CONNECTED);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    status);

  const int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - topOffset - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect listRect{0, topOffset, pageWidth, contentHeight};

  if (view == View::Menu) {
    GUI.drawList(
        renderer, listRect, static_cast<int>(menuRows.size()), menuIndex,
        [this](int i) { return std::string(I18N.get(menuRows[i].label)); }, nullptr, nullptr,
        [this](int i) { return menuValue(i); }, true);
  } else if (view == View::Layout) {
    GUI.drawList(
        renderer, listRect, blelayout::count(), layoutIndex,
        [](int i) { return std::string(I18N.get(blelayout::at(static_cast<uint8_t>(i)).nameId)); }, nullptr, nullptr,
        nullptr, false);
  } else if (view == View::Scan) {
    // Free2/3 remotes only advertise in the right slider mode — tell the user how.
    GUI.drawHelpText(renderer, Rect{0, topOffset, pageWidth, 16}, tr(STR_BT_FREE_HINT1));
    GUI.drawHelpText(renderer, Rect{0, topOffset + 16, pageWidth, 16}, tr(STR_BT_FREE_HINT2));
    const int scanTop = topOffset + 38;
    const int count = BleHid.deviceCount();
    if (count == 0) {
      GUI.drawHelpText(renderer, Rect{0, scanTop, pageWidth, 24},
                       BleHid.isScanning() ? tr(STR_SCANNING) : tr(STR_BT_NO_DEVICES));
    } else {
      GUI.drawList(
          renderer, Rect{0, scanTop, pageWidth, contentHeight - 38}, count, scanIndex,
          [this](int i) { return deviceLabel(i); }, nullptr, nullptr, nullptr, false);
    }
  } else {  // Paired
    const int count = BleHid.pairedCount();
    if (count == 0) {
      GUI.drawHelpText(renderer, Rect{0, topOffset + metrics.verticalSpacing, pageWidth, 24}, tr(STR_BT_NO_PAIRED));
    } else {
      // The live link is marked in the value column so it is obvious which bond
      // is in use and which ones Confirm would switch to.
      GUI.drawList(
          renderer, listRect, count, pairedIndex, [this](int i) { return pairedLabel(i); }, nullptr, nullptr,
          [this](int i) -> std::string { return pairedIsConnected(i) ? tr(STR_BT_CONNECTED) : ""; }, true);
    }
  }

  // Transient banner above the hints.
  if (!banner.empty()) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20}, banner.c_str());
  }

  // In the paired list, Confirm connects and a hold forgets - surface the hold hint.
  if (view == View::Paired && BleHid.pairedCount() > 0 && banner.empty()) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20},
                     tr(STR_BT_FORGET_PROMPT));
  }

  // Button hints differ by view (Menu/Layout select; Scan and Paired both connect).
  const bool scanCanRestart = view == View::Scan && BleHid.deviceCount() == 0 && !BleHid.isScanning();
  const char* confirm = (view == View::Menu || view == View::Layout) ? tr(STR_SELECT)
                        : scanCanRestart                             ? tr(STR_BT_SCAN_BUTTON)
                                                                     : tr(STR_CONNECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
