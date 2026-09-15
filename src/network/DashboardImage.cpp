#include "DashboardImage.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "network/HttpDownloader.h"
#if CROSSPOINT_FEATURE_DASHBOARD
#include "network/DashboardSet.h"
#endif
#if CROSSPOINT_FEATURE_TRMNL
#include "network/TrmnlClient.h"
#endif

namespace {
constexpr const char* kTmpPath = "/.crosspoint/dashboard.tmp";
// What x4-dashboard-server publishes under its root, in the order tried.
constexpr const char* kManifestName = "screens.json";
constexpr const char* kSingleImageName = "dashboard.bmp";

bool endsWithNoCase(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; i++) {
    if (tolower(static_cast<unsigned char>(s[s.size() - n + i])) != tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

#if CROSSPOINT_FEATURE_DASHBOARD
// Single-image fetch into the cache slot. downloadToFile removes a
// pre-existing destination before writing and deletes partial output on
// failure, so the temp file needs no cleanup here.
bool fetchSingle(const std::string& url) {
  const auto err = HttpDownloader::downloadToFile(url, kTmpPath);
  if (err != HttpDownloader::OK) {
    LOG_ERR("DASH", "Dashboard fetch failed (%d): %s", static_cast<int>(err), url.c_str());
    return false;
  }
  return DashboardImage::promoteToCache(kTmpPath);
}
#endif
}  // namespace

DashboardImage::Source DashboardImage::activeSource() {
#if CROSSPOINT_FEATURE_DASHBOARD && CROSSPOINT_FEATURE_TRMNL
  return SETTINGS.dashboardSource == CrossPointSettings::DASHBOARD_SOURCE_TRMNL ? Source::Trmnl : Source::X4;
#elif CROSSPOINT_FEATURE_DASHBOARD
  return Source::X4;
#elif CROSSPOINT_FEATURE_TRMNL
  return Source::Trmnl;
#else
  return Source::None;
#endif
}

DashboardImage::X4Urls DashboardImage::x4Urls() {
  X4Urls out;
  std::string addr = SETTINGS.dashboardUrl;
  size_t start = 0;
  while (start < addr.size() && addr[start] == ' ') ++start;
  addr.erase(0, start);
  while (!addr.empty() && (addr.back() == ' ' || addr.back() == '/')) addr.pop_back();
  if (addr.empty()) return out;

  // "192.168.1.20:8080" is enough; the scheme is implied for a LAN server.
  if (addr.find("://") == std::string::npos) addr = "http://" + addr;

  if (endsWithNoCase(addr, ".json")) {
    out.manifest = addr;
  } else if (endsWithNoCase(addr, ".bmp")) {
    out.bmp = addr;
  } else {
    out.manifest = addr + "/" + kManifestName;
    out.bmp = addr + "/" + kSingleImageName;
  }
  return out;
}

bool DashboardImage::isConfigured() {
  switch (activeSource()) {
#if CROSSPOINT_FEATURE_TRMNL
    case Source::Trmnl:
      return TrmnlClient::isConfigured();
#endif
#if CROSSPOINT_FEATURE_DASHBOARD
    case Source::X4: {
      const X4Urls urls = x4Urls();
      return !urls.manifest.empty() || !urls.bmp.empty();
    }
#endif
    default:
      return false;
  }
}

bool DashboardImage::hasCachedImage() { return !currentImagePath().empty(); }

std::string DashboardImage::currentImagePath() {
#if CROSSPOINT_FEATURE_DASHBOARD
  if (activeSource() == Source::X4) {
    // The selection is only meaningful while the file behind it exists: a
    // screen can be deleted from the card between the pick and the render.
    const std::string& picked = APP_STATE.dashboardScreenId;
    if (!picked.empty()) {
      const std::string path = DashboardSet::pathFor(picked);
      if (Storage.exists(path.c_str())) return path;
    }
    // No pick yet, or the picked screen is gone: the first screen on the card
    // keeps the sleep screen drawing something. No screens at all means the
    // server runs in single-image mode and the cache slot below applies.
    std::vector<DashboardSet::Screen> screens;
    DashboardSet::scan(screens);
    if (!screens.empty()) return DashboardSet::pathFor(screens.front().id);
  }
#endif

  return Storage.exists(kCachePath) ? std::string(kCachePath) : std::string();
}

bool DashboardImage::promoteToCache(const char* tmpPath) {
  if (Storage.exists(kCachePath) && !Storage.remove(kCachePath)) {
    LOG_ERR("DASH", "Failed to remove stale dashboard cache");
    Storage.remove(tmpPath);
    return false;
  }
  if (!Storage.rename(tmpPath, kCachePath)) {
    LOG_ERR("DASH", "Failed to move dashboard image into cache");
    Storage.remove(tmpPath);
    return false;
  }
  LOG_INF("DASH", "Dashboard image updated");
  return true;
}

bool DashboardImage::fetchToCache() {
  switch (activeSource()) {
#if CROSSPOINT_FEATURE_TRMNL
    case Source::Trmnl:
      return TrmnlClient::fetchToCache();
#endif
#if CROSSPOINT_FEATURE_DASHBOARD
    case Source::X4: {
      const X4Urls urls = x4Urls();
      if (urls.manifest.empty() && urls.bmp.empty()) {
        LOG_INF("DASH", "No dashboard server address configured");
        return false;
      }
      // Deliberately one screen, not the set: this runs on the way into sleep,
      // where the only image about to be displayed is the selected one. The
      // full sync is a user action (syncAll).
      if (!urls.manifest.empty()) {
        if (DashboardSet::syncOne(APP_STATE.dashboardScreenId, urls.manifest)) return true;
        // A card that already holds screens shows one of them, so a single
        // image would be fetched for nothing. Only a server without a manifest
        // falls through to dashboard.bmp.
        if (DashboardSet::hasScreens()) return false;
      }
      return !urls.bmp.empty() && fetchSingle(urls.bmp);
    }
#endif
    default:
      return false;
  }
}

size_t DashboardImage::syncAll() {
  switch (activeSource()) {
#if CROSSPOINT_FEATURE_TRMNL
    case Source::Trmnl:
      return TrmnlClient::fetchToCache() ? 1 : 0;
#endif
#if CROSSPOINT_FEATURE_DASHBOARD
    case Source::X4: {
      const X4Urls urls = x4Urls();
      // Manifest first: a server that has one is a screen set. No manifest (or
      // an empty one) means single-image mode, served as dashboard.bmp.
      if (!urls.manifest.empty()) {
        const size_t written = DashboardSet::syncAll(urls.manifest);
        if (written > 0) return written;
      }
      return (!urls.bmp.empty() && fetchSingle(urls.bmp)) ? 1 : 0;
    }
#endif
    default:
      return 0;
  }
}
