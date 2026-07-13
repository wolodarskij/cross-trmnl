#pragma once

/**
 * TRMNL / BYOS dashboard client. Speaks the TRMNL device API against a
 * self-hosted BYOS server (terminus, byos_fastapi, ...):
 *
 *   GET {base}/api/setup    (ID header)              -> {api_key, ...}
 *   GET {base}/api/display  (ID + Access-Token)      -> {image_url, ...}
 *
 * then downloads image_url and converts it into the shared dashboard cache
 * (DashboardImage::kCachePath). PNG images are converted to a dithered BMP
 * via PngToBmpConverter; BMP images are cached as-is. TRMNL images are
 * landscape 800x480 — rotation happens at render time (see the dashboard
 * render paths), not here.
 *
 * WiFi must already be connected. An empty trmnlApiKey auto-provisions via
 * /api/setup and persists the returned key.
 */
class TrmnlClient {
 public:
  // True when SETTINGS.trmnlUrl is set.
  static bool isConfigured();

  // Full fetch flow into DashboardImage::kCachePath. Returns true when the
  // cache now holds a fresh image; on failure the previous cache is kept.
  static bool fetchToCache();
};
