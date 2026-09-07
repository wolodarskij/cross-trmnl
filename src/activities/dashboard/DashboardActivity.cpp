#include "DashboardActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/DashboardImage.h"

bool DashboardActivity::usingScreenSet() const {
  return SETTINGS.dashboardSource == CrossPointSettings::DASHBOARD_SOURCE_SCREENSET;
}

void DashboardActivity::onEnter() {
  Activity::onEnter();
  syncAttempted = false;
  syncSucceeded = false;

  // No WiFi, no fetch, no waiting: whatever is on the card is shown at once.
  // Connecting is what Confirm is for.
  reloadScreens();
  state = currentPath().empty() ? EMPTY : SHOWING;
  requestUpdate();
}

void DashboardActivity::onExit() {
  Activity::onExit();

  // Written here rather than per keypress. The in-memory id is updated as the
  // user moves, which is what the sleep path reads (it saves state before the
  // outgoing activity's onExit runs), so this only has to outlive the reboot.
  if (selectionChanged) {
    APP_STATE.saveToFile();
    selectionChanged = false;
  }

  // Same convention as ClockSyncActivity: if this activity brought WiFi up,
  // tear it down and silently restart to clear heap fragmentation.
  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void DashboardActivity::reloadScreens() {
  screens.clear();
  selected = 0;
  if (!usingScreenSet()) return;  // legacy sources own one image, not a set

  DashboardSet::scan(screens);
  if (screens.empty()) return;

  // Restore by id, not by index: a sync can add screens ahead of the pinned
  // one, and an index would silently start showing a different dashboard.
  const std::string& picked = APP_STATE.dashboardScreenId;
  if (!picked.empty()) {
    const auto it =
        std::find_if(screens.begin(), screens.end(), [&](const DashboardSet::Screen& s) { return s.id == picked; });
    if (it != screens.end()) selected = static_cast<int>(std::distance(screens.begin(), it));
  }
  applySelection();
}

void DashboardActivity::applySelection() {
  if (screens.empty()) return;
  // Memory only. Costs nothing per press, and leaves the sleep path and the
  // renderer looking at the same screen the user is.
  APP_STATE.dashboardScreenId = screens[selected].id;
}

void DashboardActivity::step(const int delta) {
  const int count = static_cast<int>(screens.size());
  if (count < 2) return;
  selected = (selected + delta % count + count) % count;
  applySelection();
  selectionChanged = true;
  requestUpdate();
}

std::string DashboardActivity::currentPath() const {
  if (usingScreenSet()) {
    if (screens.empty()) return {};
    return DashboardSet::pathFor(screens[selected].id);
  }
  return DashboardImage::currentImagePath();
}

void DashboardActivity::launchWifiSelection() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             // Backing out of the network picker is not a
                             // reason to leave: the screens already on the
                             // card are still perfectly usable offline.
                             state = currentPath().empty() ? EMPTY : SHOWING;
                             requestUpdate();
                             return;
                           }
                           state = SYNCING;
                           requestUpdate();
                         });
}

void DashboardActivity::runSync() {
  syncAttempted = true;
  syncSucceeded = false;

  if (WiFi.status() == WL_CONNECTED) {
    syncSucceeded = usingScreenSet() ? DashboardSet::syncAll() > 0 : DashboardImage::fetchToCache();
  }

  // Rescan either way: a partial sync still added files worth listing, and a
  // failed one must not drop the screens that were already there.
  reloadScreens();
  state = currentPath().empty() ? EMPTY : SHOWING;
  requestUpdate();
}

void DashboardActivity::loop() {
  if (state == SYNCING) {
    // First tick renders the progress screen, then the blocking work runs
    // (mirrors ClockSyncActivity's sync flow).
    requestUpdateAndWait();
    runSync();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!DashboardImage::isConfigured()) {
      // Nothing to sync against. Say so instead of starting a WiFi flow that
      // could only end in the same message.
      syncAttempted = true;
      syncSucceeded = false;
      requestUpdate();
      return;
    }
    state = SYNCING;
    if (WiFi.status() != WL_CONNECTED) {
      shouldTearDownWifiOnExit = true;
      launchWifiSelection();
      return;
    }
    requestUpdate();
    return;
  }

  // Up/Down double as prev/next the same way they do in the image viewer, so
  // the gesture works whichever way the device is being held.
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    step(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    step(1);
  }
}

void DashboardActivity::render(RenderLock&&) {
  const auto pageHeight = renderer.getScreenHeight();

  if (state == SYNCING) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2,
                              usingScreenSet() ? tr(STR_DASHBOARD_SYNCING) : tr(STR_DASHBOARD_FETCHING));
    renderer.displayBuffer();
    return;
  }

  if (state == EMPTY) {
    renderer.clearScreen();
    if (!DashboardImage::isConfigured()) {
      renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20,
                                usingScreenSet() ? tr(STR_DASHBOARD_NO_SCREENS) : tr(STR_DASHBOARD_NO_URL), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10,
                                usingScreenSet() ? tr(STR_DASHBOARD_NO_SERVER) : tr(STR_CHECK_SERIAL_OUTPUT));
    } else if (syncAttempted && !syncSucceeded) {
      renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_DASHBOARD_SYNC_FAILED), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));
    } else {
      // Configured, nothing fetched yet. The two sources need different
      // advice: a screen set can be filled by hand from the card, a single
      // image URL can only be downloaded.
      renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20,
                                usingScreenSet() ? tr(STR_DASHBOARD_NO_SCREENS) : tr(STR_DASHBOARD_NO_IMAGE), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10,
                                usingScreenSet() ? tr(STR_DASHBOARD_ADD_SCREENS) : tr(STR_DASHBOARD_PRESS_SYNC));
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DASHBOARD_SYNC), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  renderImage(currentPath());
}

// Draws a dashboard BMP full-screen, same scale/center rules as
// BmpViewerActivity. An unreadable file draws the failure text but leaves the
// state alone, so Left/Right still move off a broken screen.
void DashboardActivity::renderImage(const std::string& path) {
  const auto pageHeight = renderer.getScreenHeight();

  HalFile file;
  if (path.empty() || !Storage.openFileForRead("DASH", path, file)) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_DASHBOARD_FETCH_FAILED));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_DASHBOARD_FETCH_FAILED));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  // Landscape images (e.g. TRMNL's 800x480) render full-screen by switching
  // the renderer orientation for the draw; logical dims follow it.
  const bool landscape = bitmap.getWidth() > bitmap.getHeight();
  if (landscape) renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  const auto viewW = renderer.getScreenWidth();
  const auto viewH = renderer.getScreenHeight();

  int x, y;
  if (bitmap.getWidth() > viewW || bitmap.getHeight() > viewH) {
    const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(viewW) / static_cast<float>(viewH);
    if (ratio > screenRatio) {
      x = 0;
      y = std::round((static_cast<float>(viewH) - static_cast<float>(viewW) / ratio) / 2);
    } else {
      x = std::round((static_cast<float>(viewW) - static_cast<float>(viewH) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (viewW - bitmap.getWidth()) / 2;
    y = (viewH - bitmap.getHeight()) / 2;
  }

  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, viewW, viewH, 0, 0);

  // One footer line carrying whatever the user needs to know: which screen
  // this is, and whether the last sync worked. Built as a single string
  // because two draws at the same y would overlap.
  //
  // Skipped for greyscale images: overlay text drawn only in the base pass
  // would be corrupted by the grey nudge passes below.
  const bool hasGreyscale = bitmap.hasGreyscale();
  if (!hasGreyscale) {
    char footer[96] = "";
    if (screens.size() > 1) {
      snprintf(footer, sizeof(footer), "%s  (%d/%d)", screens[selected].name.c_str(), selected + 1,
               static_cast<int>(screens.size()));
    }
    if (syncAttempted && !syncSucceeded) {
      const char* failed = usingScreenSet() ? tr(STR_DASHBOARD_SYNC_FAILED) : tr(STR_DASHBOARD_FETCH_FAILED);
      if (footer[0] != '\0') {
        const size_t used = strlen(footer);
        snprintf(footer + used, sizeof(footer) - used, "  -  %s", failed);
      } else {
        snprintf(footer, sizeof(footer), "%s", failed);
      }
    }
    if (footer[0] != '\0') renderer.drawCenteredText(SMALL_FONT_ID, viewH - 14, footer);

    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    if (landscape) renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    return;
  }

  // Greyscale two-pass render, same sequence as SleepActivity's
  // renderBitmapSleepScreen: clean B/W base paint, then LSB/MSB grey planes.
  renderer.displayGrayscaleBase(HalDisplay::FULL_REFRESH);

  bitmap.rewindToData();
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.drawBitmap(bitmap, x, y, viewW, viewH, 0, 0);
  renderer.copyGrayscaleLsbBuffers();

  bitmap.rewindToData();
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.drawBitmap(bitmap, x, y, viewW, viewH, 0, 0);
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  if (landscape) renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}
