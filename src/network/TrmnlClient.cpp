#include "TrmnlClient.h"

#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "network/DashboardImage.h"
#include "network/HttpDownloader.h"

namespace {

// Raw downloaded image (PNG or BMP, sniffed by magic bytes) and the BMP
// produced from a PNG before it is promoted into the dashboard cache.
constexpr const char* kImgTmpPath = "/.crosspoint/dashboard.img";
constexpr const char* kBmpTmpPath = "/.crosspoint/dashboard.tmp";

// Same TLS heap floor as KOReaderSyncClient: wolfSSL handshakes need working
// contiguous heap. Only relevant for https BYOS servers.
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;

std::string baseUrl() {
  std::string base = SETTINGS.trmnlUrl;
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base;
}

bool tlsHeapOk(const std::string& url) {
  if (url.rfind("https", 0) != 0) return true;
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAlloc = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_HEAP_FOR_TLS || maxAlloc < MIN_HEAP_FOR_TLS) {
    LOG_ERR("TRMNL", "Insufficient heap for TLS: %u free, %u max alloc", freeHeap, maxAlloc);
    return false;
  }
  return true;
}

// Shared device headers for both /api/setup and /api/display. TRMNL servers
// expect landscape dimensions, so report the panel's long side as Width —
// this way the same code holds on non-X4 devices with different panels.
void applyDeviceHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("Accept", "application/json");
  http.addHeader("ID", WiFi.macAddress().c_str());
  if (SETTINGS.trmnlApiKey[0] != '\0') {
    http.addHeader("Access-Token", SETTINGS.trmnlApiKey);
  }
  http.addHeader("FW-Version", CROSSPOINT_VERSION);
  http.addHeader("RSSI", std::to_string(WiFi.RSSI()));
  http.addHeader("Width", std::to_string(std::max(display.getDisplayWidth(), display.getDisplayHeight())));
  http.addHeader("Height", std::to_string(std::min(display.getDisplayWidth(), display.getDisplayHeight())));
}

// GET {base}{path}, parse the JSON body into doc. Returns HTTP status or -1.
int getJson(const std::string& url, JsonDocument& doc) {
  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("TRMNL", "Bad URL: %s", url.c_str());
    return -1;
  }
  applyDeviceHeaders(http);
  const int code = http.GET();
  if (code == 200) {
    const DeserializationError err = deserializeJson(doc, http.getString().c_str());
    if (err) {
      LOG_ERR("TRMNL", "JSON parse failed: %s", err.c_str());
      http.end();
      return -1;
    }
  }
  http.end();
  return code;
}

// Auto-provision via /api/setup when no API key is stored. A missing api_key
// in the response is non-fatal: some BYOS servers accept ID-only requests.
void ensureProvisioned(const std::string& base) {
  if (SETTINGS.trmnlApiKey[0] != '\0') return;

  JsonDocument doc;
  const int code = getJson(base + "/api/setup", doc);
  LOG_INF("TRMNL", "/api/setup -> %d", code);
  if (code != 200) return;

  const char* apiKey = doc["api_key"] | "";
  if (apiKey[0] != '\0') {
    strncpy(SETTINGS.trmnlApiKey, apiKey, sizeof(SETTINGS.trmnlApiKey) - 1);
    SETTINGS.trmnlApiKey[sizeof(SETTINGS.trmnlApiKey) - 1] = '\0';
    SETTINGS.saveToFile();
    LOG_INF("TRMNL", "Provisioned as %s", doc["friendly_id"] | "(device)");
  }
}

// Converts the downloaded image into the dashboard cache. PNGs go through
// PngToBmpConverter (dithered 2-bit BMP); BMPs are promoted as-is.
bool cacheDownloadedImage() {
  HalFile img;
  if (!Storage.openFileForRead("TRM", kImgTmpPath, img)) return false;
  uint8_t magic[2] = {0, 0};
  const bool haveMagic = img.read(magic, 2) == 2;

  if (haveMagic && magic[0] == 'B' && magic[1] == 'M') {
    img.close();
    return DashboardImage::promoteToCache(kImgTmpPath);
  }

  if (!haveMagic || magic[0] != 0x89 || magic[1] != 'P') {
    LOG_ERR("TRMNL", "Unknown image format (magic %02x %02x)", magic[0], magic[1]);
    img.close();
    Storage.remove(kImgTmpPath);
    return false;
  }

  if (!img.seek(0)) {
    img.close();
    Storage.remove(kImgTmpPath);
    return false;
  }
  HalFile bmp;
  if (!Storage.openFileForWrite("TRM", kBmpTmpPath, bmp)) {
    img.close();
    Storage.remove(kImgTmpPath);
    return false;
  }
  const int maxDim = std::max(display.getDisplayWidth(), display.getDisplayHeight());
  const bool converted = PngToBmpConverter::pngFileToBmpStreamWithSize(img, bmp, maxDim, maxDim);
  bmp.close();
  img.close();
  Storage.remove(kImgTmpPath);
  if (!converted) {
    LOG_ERR("TRMNL", "PNG conversion failed");
    Storage.remove(kBmpTmpPath);
    return false;
  }
  return DashboardImage::promoteToCache(kBmpTmpPath);
}

}  // namespace

bool TrmnlClient::isConfigured() { return SETTINGS.trmnlUrl[0] != '\0'; }

bool TrmnlClient::fetchToCache() {
  const std::string base = baseUrl();
  if (base.empty()) {
    LOG_INF("TRMNL", "No TRMNL server URL configured");
    return false;
  }
  if (!tlsHeapOk(base)) return false;

  ensureProvisioned(base);

  JsonDocument doc;
  const int code = getJson(base + "/api/display", doc);
  if (code != 200) {
    LOG_ERR("TRMNL", "/api/display -> %d", code);
    return false;
  }
  // Official servers embed an application status alongside HTTP 200.
  const int status = doc["status"] | 0;
  if (status != 0 && status != 200) {
    LOG_ERR("TRMNL", "/api/display status field: %d", status);
    return false;
  }

  std::string imageUrl = doc["image_url"] | "";
  if (imageUrl.empty()) {
    LOG_ERR("TRMNL", "No image_url in /api/display response");
    return false;
  }
  if (imageUrl.find("://") == std::string::npos) {
    std::string resolved;
    if (!freeink::SecureHttpClient::resolveUrl(base + "/", imageUrl, resolved)) {
      LOG_ERR("TRMNL", "Cannot resolve image_url: %s", imageUrl.c_str());
      return false;
    }
    imageUrl = resolved;
  }
  LOG_INF("TRMNL", "Fetching %s", imageUrl.c_str());

  if (HttpDownloader::downloadToFile(imageUrl, kImgTmpPath) != HttpDownloader::OK) {
    LOG_ERR("TRMNL", "Image download failed");
    return false;
  }
  return cacheDownloadedImage();
}
