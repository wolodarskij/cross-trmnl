#pragma once

/**
 * Shared fetch/cache logic for the networked dashboard image. Used by the
 * DASHBOARD sleep screen and the manual Dashboard activity. The image comes
 * from the source selected in settings: a plain BMP URL ("simple") or a
 * TRMNL/BYOS server (see TrmnlClient).
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

  // Atomically replaces the cache with tmpPath (removes the temp on failure).
  static bool promoteToCache(const char* tmpPath);
};
