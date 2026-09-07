#include "DashboardImage.h"

#include <HalStorage.h>
#include <Logging.h>

#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "network/DashboardSet.h"
#include "network/HttpDownloader.h"
#include "network/TrmnlClient.h"

namespace {
constexpr const char* kTmpPath = "/.crosspoint/dashboard.tmp";

bool usingScreenSet() { return SETTINGS.dashboardSource == CrossPointSettings::DASHBOARD_SOURCE_SCREENSET; }
}  // namespace

bool DashboardImage::isConfigured() {
  if (usingScreenSet()) return DashboardSet::isConfigured();
  if (SETTINGS.dashboardSource == CrossPointSettings::DASHBOARD_SOURCE_TRMNL) {
    return TrmnlClient::isConfigured();
  }
  return SETTINGS.dashboardUrl[0] != '\0';
}

bool DashboardImage::hasCachedImage() { return !currentImagePath().empty(); }

std::string DashboardImage::currentImagePath() {
  if (usingScreenSet()) {
    // The selection is only meaningful while the file behind it exists: a
    // screen can be deleted from the card between the pick and the render.
    const std::string& picked = APP_STATE.dashboardScreenId;
    if (!picked.empty()) {
      const std::string path = DashboardSet::pathFor(picked);
      if (Storage.exists(path.c_str())) return path;
    }
    // No pick yet, or the picked screen is gone: fall back to the first
    // screen on the card so the sleep screen still has something to draw.
    std::vector<DashboardSet::Screen> screens;
    DashboardSet::scan(screens);
    if (!screens.empty()) return DashboardSet::pathFor(screens.front().id);
  }

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
  if (usingScreenSet()) {
    // Deliberately one screen, not the set: this runs on the way into sleep,
    // where the only image about to be displayed is the selected one. Pulling
    // the whole set here would multiply the radio time by the screen count
    // for images nothing is going to draw. The full sync is a user action.
    return DashboardSet::syncOne(APP_STATE.dashboardScreenId);
  }
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
