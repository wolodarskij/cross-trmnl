#include "DashboardImage.h"

#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "network/HttpDownloader.h"
#include "network/TrmnlClient.h"

namespace {
constexpr const char* kTmpPath = "/.crosspoint/dashboard.tmp";
}

bool DashboardImage::isConfigured() {
  if (SETTINGS.dashboardSource == CrossPointSettings::DASHBOARD_SOURCE_TRMNL) {
    return TrmnlClient::isConfigured();
  }
  return SETTINGS.dashboardUrl[0] != '\0';
}

bool DashboardImage::hasCachedImage() { return Storage.exists(kCachePath); }

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
  if (SETTINGS.dashboardSource == CrossPointSettings::DASHBOARD_SOURCE_TRMNL) {
    return TrmnlClient::fetchToCache();
  }

  if (SETTINGS.dashboardUrl[0] == '\0') {
    LOG_INF("DASH", "No dashboard URL configured");
    return false;
  }

  // downloadToFile removes a pre-existing destination before writing and
  // deletes partial output on failure, so the temp file needs no cleanup here.
  const auto err = HttpDownloader::downloadToFile(SETTINGS.dashboardUrl, kTmpPath);
  if (err != HttpDownloader::OK) {
    LOG_ERR("DASH", "Dashboard fetch failed (%d): %s", static_cast<int>(err), SETTINGS.dashboardUrl);
    return false;
  }
  return promoteToCache(kTmpPath);
}
