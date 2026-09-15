#pragma once

#include <string>

#include "Features.h"

/**
 * Shared fetch/cache logic for the networked dashboard image, used by the
 * DASHBOARD sleep-screen modes and the Dashboard activity. Two sources, each
 * a build-time option (Features.h) and, when both are present, a setting:
 *
 *  - x4-dashboard-server (CROSSPOINT_FEATURE_DASHBOARD). One address, the
 *    server root. The server publishes a screen set at {base}/screens.json
 *    and a single image at {base}/dashboard.bmp; the manifest is tried first
 *    and the BMP is the fallback, so one address covers both server modes and
 *    the user never has to say which one they run. An address that already
 *    ends in .json or .bmp is used as-is. Screens land in /dashboards (see
 *    DashboardSet); a single image lands in kCachePath.
 *  - TRMNL / BYOS (CROSSPOINT_FEATURE_TRMNL): TrmnlClient, into kCachePath.
 *
 * Callers must not assume the image lives at kCachePath: ask
 * currentImagePath() where it actually is.
 */
class DashboardImage {
 public:
  // Cached copy of the last successfully fetched single image. Survives failed
  // refreshes so the sleep screen can fall back to the previous image.
  static constexpr const char* kCachePath = "/.crosspoint/dashboard.bmp";

  enum class Source { None, X4, Trmnl };
  // The source in use: the setting when both are compiled in, otherwise the
  // one that is (the setting is then ignored, so a stale value cannot select
  // code that is not there).
  static Source activeSource();

  // True when the active source has an address configured.
  static bool isConfigured();

  // Sleep path: refresh only the image about to be displayed. WiFi must
  // already be connected. Work happens in temp files so a failure keeps the
  // previous image intact. Returns true when a fresh image was written.
  static bool fetchToCache();

  // User action (the Dashboard activity's Sync): the whole screen set when
  // the server has one, else the single image. Returns images written.
  static size_t syncAll();

  // True when there is an image on the SD card to show.
  static bool hasCachedImage();

  // Absolute path of the image to display right now, or empty when there is
  // nothing to show. With screens on the card this follows the user's pick in
  // /dashboards; otherwise it is the single-image cache.
  static std::string currentImagePath();

  // Atomically replaces the cache with tmpPath (removes the temp on failure).
  static bool promoteToCache(const char* tmpPath);

  // The x4-dashboard-server URLs derived from SETTINGS.dashboardUrl. Either
  // member is empty when that mode does not apply (unset address, or an
  // address naming a .json / .bmp explicitly).
  struct X4Urls {
    std::string manifest;
    std::string bmp;
  };
  static X4Urls x4Urls();
};
