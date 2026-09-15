#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Bluetooth page-turner / keyboard settings. One screen with four views:
//   Menu   — enable/disable BT, scan & pair, disconnect, paired devices, map
//            remote buttons, keyboard layout, hide on-screen keyboard.
//   Scan   — live list of discovered BLE HID devices; Confirm connects.
//   Paired — bonded devices; Confirm connects, hold Confirm forgets.
//   Layout — keyboard layout picker (see BleKeyboardLayouts.h).
// All BLE access goes through the FreeInk BleHid singleton; everything no-ops
// gracefully when BLE is compiled out (BleHid.begin() returns false).
class BluetoothSettingsActivity final : public Activity {
 public:
  explicit BluetoothSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BluetoothSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool keepsBluetoothAlive() const override { return true; }

 private:
  enum class View { Menu, Scan, Paired, Layout };

  // Menu row actions.
  enum class Action { ToggleBt, Scan, Disconnect, MapButtons, PairedDevices, KeyboardLayout, HideOsk };
  struct MenuRow {
    Action action;
    StrId label;
  };

  View view = View::Menu;
  std::vector<MenuRow> menuRows;
  int menuIndex = 0;
  int scanIndex = 0;
  int pairedIndex = 0;
  int layoutIndex = 0;

  ButtonNavigator buttonNavigator;

  // Transient status banner (connect result, forget confirmation, etc.).
  std::string banner;
  unsigned long bannerUntil = 0;

  // Set when a connect() has been issued and we're waiting for the async result.
  bool awaitingConnect = false;
  // Guards the Paired view's hold-to-forget so it fires once per hold and suppresses
  // the tap-to-connect on the same press.
  bool pairedActionTaken = false;
  bool lastLoggedScanState = false;
  uint8_t lastLoggedDeviceCount = 0xFF;

  void rebuildMenuRows();
  void handleMenuConfirm();
  void startScanView();
  void selectLayout(int index);
  void setBanner(const char* text);

  std::string deviceLabel(int index) const;  // scan list row text
  std::string pairedLabel(int index) const;  // paired list row text
  std::string menuValue(int index) const;    // right-hand value of a menu row
  // True when the paired entry at `index` is the device currently connected.
  bool pairedIsConnected(int index) const;
  // Issue an async connect and arm the result watcher; banners a refused request.
  void requestConnect(const char* addr);
};
