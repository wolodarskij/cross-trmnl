#include "DashboardActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <cmath>

#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/DashboardImage.h"

void DashboardActivity::onEnter() {
  Activity::onEnter();
  state = FETCHING;
  fetchSucceeded = false;

  if (!DashboardImage::isConfigured() && !DashboardImage::hasCachedImage()) {
    state = FAILED;
    requestUpdate();
    return;
  }

  if (!DashboardImage::isConfigured() || WiFi.status() == WL_CONNECTED) {
    // Nothing to fetch (cache only) or WiFi already up: go straight to work.
    requestUpdate();
    return;
  }

  shouldTearDownWifiOnExit = true;
  launchWifiSelection();
}

void DashboardActivity::onExit() {
  Activity::onExit();

  // Same convention as ClockSyncActivity: if this activity brought WiFi up,
  // tear it down and silently restart to clear heap fragmentation.
  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void DashboardActivity::launchWifiSelection() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled && !DashboardImage::hasCachedImage()) {
                             finish();
                             return;
                           }
                           state = FETCHING;
                           requestUpdate();
                         });
}

void DashboardActivity::runFetch() {
  fetchSucceeded = false;
  if (DashboardImage::isConfigured() && WiFi.status() == WL_CONNECTED) {
    fetchSucceeded = DashboardImage::fetchToCache();
  }

  // A stale cached image is still better than an error screen: keep showing
  // it when the refresh fails and report the failure via the hint line.
  state = DashboardImage::hasCachedImage() ? SHOWING : FAILED;
  requestUpdate();
}

void DashboardActivity::loop() {
  if (state == FETCHING) {
    // First tick: render the "Updating..." screen, then perform the blocking
    // fetch (mirrors ClockSyncActivity's sync flow).
    requestUpdateAndWait();
    runFetch();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    state = FETCHING;
    if (DashboardImage::isConfigured() && WiFi.status() != WL_CONNECTED) {
      shouldTearDownWifiOnExit = true;
      launchWifiSelection();
      return;
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void DashboardActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (state == FETCHING) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_DASHBOARD_FETCHING));
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.clearScreen();
    if (!DashboardImage::isConfigured()) {
      renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_DASHBOARD_NO_URL), true,
                                EpdFontFamily::BOLD);
    } else {
      renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_DASHBOARD_FETCH_FAILED), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  // SHOWING: render the cached image full-screen (same scale/center rules as
  // BmpViewerActivity). On an unreadable cache just draw the failure text;
  // state stays SHOWING so Confirm still retries the fetch.
  HalFile file;
  if (!Storage.openFileForRead("DASH", DashboardImage::kCachePath, file)) {
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
  const bool hasGreyscale = bitmap.hasGreyscale();
  if (!fetchSucceeded && !hasGreyscale) {
    // Refresh failed but an older image is cached: flag it unobtrusively.
    // Skipped for greyscale images: overlay text drawn only in the base pass
    // would be corrupted by the grey nudge passes below.
    renderer.drawCenteredText(SMALL_FONT_ID, viewH - 14, tr(STR_DASHBOARD_FETCH_FAILED));
  }

  if (!hasGreyscale) {
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
