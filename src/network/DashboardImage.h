#pragma once

#include <string>

/**
 * Shared fetch/cache logic for the networked dashboard image. Used by the
 * DASHBOARD sleep screen and the manual Dashboard activity. The image comes
 * from the source selected in settings: a plain BMP URL ("simple"), a
 * TRMNL/BYOS server (see TrmnlClient), or a multi-screen set (DashboardSet).
 *
 * The first two sources own a single cache slot, kCachePath. The screen set
 * keeps many files in /dashboards instead, so callers must not assume the
 * image lives at kCachePath - ask currentImagePath() where it actually is.
 */
class DashboardImage {
 public:
  // Cached copy of the last successfully fetched image. Survives failed
  // refreshes so the sleep screen can fall back to the previous image.
  static constexpr const char* kCachePath = "/.crosspoint/dashboard.bmp";

  // True when the active dashboard source has an address configured.
  static bool isConfigured();

  // Fetches from the active source into kCachePath. WiFi must already be
  // connected. Work happens in temp files so a failure keeps the previous
  // cached image intact. Returns true when the cache now holds a fresh image.
  static bool fetchToCache();

  // True when a previously fetched image exists on the SD card.
  static bool hasCachedImage();

  // Absolute path of the image to display right now, or an empty string when
  // there is nothing to show. For a screen set this follows the user's
  // selection in /dashboards and falls back to the legacy cache when that
  // directory is empty, which is what keeps a device that switched sources
  // showing something rather than a blank screen.
  static std::string currentImagePath();

  // Atomically replaces the cache with tmpPath (removes the temp on failure).
  static bool promoteToCache(const char* tmpPath);
};
