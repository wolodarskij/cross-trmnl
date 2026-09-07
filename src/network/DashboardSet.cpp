#include "DashboardSet.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "network/HttpDownloader.h"

namespace {

constexpr const char* kBmpExt = ".bmp";
// Sidecar, not a directory entry: a leading dot keeps it out of the scan and
// out of the user's way when they browse the card.
constexpr const char* kNameIndexPath = "/dashboards/.index";
constexpr const char* kTmpPath = "/dashboards/.download.tmp";

// A manifest large enough to matter is a malfunctioning server, not a big
// dashboard set. Cap the body so a bad response cannot exhaust the heap
// before the parser ever sees it.
constexpr size_t kMaxManifestBytes = 32 * 1024;

std::string baseUrl() {
  std::string base = SETTINGS.dashboardSetUrl;
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base;
}

// Ids reach the filesystem as path components, so they are validated rather
// than escaped: the contract already limits them to [a-z0-9._-], and anything
// outside that is a server bug worth failing loudly on instead of sanitising
// into a different file. Leading dots are refused as well - they would land
// the screen next to the sidecar, hidden from the scan that has to find it.
bool isSafeId(const std::string& id) {
  if (id.empty() || id.size() > DashboardSet::kMaxIdLen) return false;
  if (id.front() == '.') return false;
  for (const char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// Screen urls are documented as relative to the server root, but absolute
// ones are accepted too so a manifest can point at a separate image host.
std::string resolveUrl(const std::string& base, const std::string& url) {
  if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return url;
  if (url.empty()) return "";
  return url.front() == '/' ? base + url : base + "/" + url;
}

// Streams the body instead of buffering it wholesale, so the cap above is
// enforced while the transfer is in flight rather than after it lands.
bool fetchBounded(const std::string& url, std::string& out) {
  out.clear();
  bool overflowed = false;
  const bool ok = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, const size_t len) {
    if (out.size() + len > kMaxManifestBytes) {
      overflowed = true;
      return false;
    }
    out.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (overflowed) {
    LOG_ERR("DSET", "Manifest exceeds %u bytes", static_cast<unsigned>(kMaxManifestBytes));
    return false;
  }
  return ok && !out.empty();
}

// Fetches and validates {base}/screens.json. The document stays with the
// caller so downloads can read each screen's own url straight out of it -
// copying those urls into a list first would hold a third string per screen
// for the whole sync, to no end.
//
// Only the fields the store needs survive the filter. The manifest also
// carries sizes, revisions and timestamps, which matter to clients that cache
// or rotate on a timer; parsing them here would cost heap for values nothing
// reads.
bool fetchManifest(JsonDocument& doc) {
  const std::string base = baseUrl();
  if (base.empty()) {
    LOG_INF("DSET", "No dashboard server URL configured");
    return false;
  }

  std::string body;
  if (!fetchBounded(base + "/screens.json", body)) {
    LOG_ERR("DSET", "Cannot fetch screens.json from %s", base.c_str());
    return false;
  }

  JsonDocument filter;
  filter["format"] = true;
  filter["active"] = true;
  JsonObject screenFilter = filter["screens"].add<JsonObject>();
  screenFilter["id"] = true;
  screenFilter["name"] = true;
  screenFilter["url"] = true;

  const DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    LOG_ERR("DSET", "Manifest parse failed: %s", err.c_str());
    return false;
  }

  const char* format = doc["format"] | "";
  if (strcmp(format, "x4-dashboard-set") != 0) {
    LOG_ERR("DSET", "Not a screen set (format '%s')", format);
    return false;
  }
  return true;
}

// Downloads one screen into the store via a temp file, so a failed or
// truncated transfer leaves the previous copy in place.
bool download(const std::string& id, const std::string& url) {
  const std::string resolved = resolveUrl(baseUrl(), url);
  if (resolved.empty()) return false;

  const auto err = HttpDownloader::downloadToFile(resolved, kTmpPath);
  if (err != HttpDownloader::OK) {
    LOG_ERR("DSET", "Fetch failed (%d): %s", static_cast<int>(err), resolved.c_str());
    return false;
  }

  const std::string dest = DashboardSet::pathFor(id);
  if (Storage.exists(dest.c_str()) && !Storage.remove(dest.c_str())) {
    LOG_ERR("DSET", "Cannot replace %s", dest.c_str());
    Storage.remove(kTmpPath);
    return false;
  }
  if (!Storage.rename(kTmpPath, dest.c_str())) {
    LOG_ERR("DSET", "Cannot move screen into %s", dest.c_str());
    Storage.remove(kTmpPath);
    return false;
  }
  return true;
}

// Persists id -> name so the picker can label screens after a reboot; the
// BMPs themselves carry no name.
void writeNameIndex(const JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForWrite("DSET", kNameIndexPath, file)) return;
  for (JsonObjectConst obj : doc["screens"].as<JsonArrayConst>()) {
    const char* id = obj["id"] | "";
    const char* name = obj["name"] | "";
    if (id[0] == '\0' || name[0] == '\0') continue;
    file.print(id);
    file.print("|");
    file.println(name);
  }
}

// Applies the id -> name labels written by the last sync. A missing or stale
// index is not an error: scan() has already defaulted every name to its id,
// so the picker stays usable either way.
//
// The whole file is read in one bounded allocation rather than byte-by-byte
// through HalFile: every HalFile call takes the storage mutex, and a few
// thousand of them to read a couple of KB is a poor trade for the ~3KB this
// holds for the length of the call.
void applyNameIndex(std::vector<DashboardSet::Screen>& screens) {
  if (screens.empty()) return;

  constexpr size_t kMaxBytes = DashboardSet::kMaxScreens * (DashboardSet::kMaxIdLen + 64);
  auto buf = makeUniqueNoThrow<char[]>(kMaxBytes + 1);
  if (!buf) {
    LOG_ERR("DSET", "OOM reading name index");
    return;
  }

  const size_t read = Storage.readFileToBuffer(kNameIndexPath, buf.get(), kMaxBytes + 1);
  if (read == 0) return;
  buf[read] = '\0';

  // Parsed in place: each line is "id|name", and the separators are simply
  // overwritten with terminators rather than copied out into new strings.
  char* cursor = buf.get();
  while (cursor && *cursor) {
    char* eol = strpbrk(cursor, "\r\n");
    if (eol) {
      *eol = '\0';
      ++eol;
      while (*eol == '\r' || *eol == '\n') ++eol;
    }

    char* sep = strchr(cursor, '|');
    if (sep && sep[1] != '\0') {
      *sep = '\0';
      const auto it =
          std::find_if(screens.begin(), screens.end(), [&](const DashboardSet::Screen& s) { return s.id == cursor; });
      if (it != screens.end()) it->name = sep + 1;
    }
    cursor = eol;
  }
}

bool ensureStoreDir() {
  if (Storage.exists(DashboardSet::kScreensDir)) return true;
  if (Storage.mkdir(DashboardSet::kScreensDir)) return true;
  LOG_ERR("DSET", "Cannot create %s", DashboardSet::kScreensDir);
  return false;
}

}  // namespace

std::string DashboardSet::pathFor(const std::string& id) { return std::string(kScreensDir) + "/" + id + kBmpExt; }

bool DashboardSet::isConfigured() { return SETTINGS.dashboardSetUrl[0] != '\0'; }

void DashboardSet::scan(std::vector<Screen>& out) {
  out.clear();
  if (!ensureStoreDir()) return;

  auto dir = Storage.open(kScreensDir);
  if (!dir || !dir.isDirectory()) return;

  // One growth event instead of five: the cap is the worst case and it is
  // small enough that reserving it outright beats repeated reallocation.
  out.reserve(kMaxScreens);

  char name[256];
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (out.size() >= kMaxScreens) break;
    if (f.isDirectory()) continue;
    f.getName(name, sizeof(name));
    // Skips the sidecar and the in-flight temp file along with anything else
    // hidden, which is why both are named with a leading dot.
    if (name[0] == '.') continue;
    if (!FsHelpers::checkFileExtension(std::string_view{name}, kBmpExt)) continue;

    std::string id(name);
    id.resize(id.size() - strlen(kBmpExt));  // the stem is the id
    if (id.empty()) continue;
    out.push_back({id, id});
  }

  std::sort(out.begin(), out.end(), [](const Screen& a, const Screen& b) { return a.id < b.id; });

  // Labels are applied after the sort so ordering stays keyed on the id,
  // which is stable, rather than on a name the user can change.
  applyNameIndex(out);
}

size_t DashboardSet::syncAll() {
  JsonDocument doc;
  if (!fetchManifest(doc)) return 0;
  if (!ensureStoreDir()) return 0;

  size_t written = 0;
  size_t seen = 0;
  for (JsonObjectConst obj : doc["screens"].as<JsonArrayConst>()) {
    if (seen >= kMaxScreens) {
      LOG_INF("DSET", "Manifest lists more than %u screens; ignoring the rest", static_cast<unsigned>(kMaxScreens));
      break;
    }
    ++seen;
    const std::string id = obj["id"] | "";
    if (!isSafeId(id)) {
      LOG_ERR("DSET", "Skipping screen with unusable id '%s'", id.c_str());
      continue;
    }
    // One screen failing does not abort the run: the rest of the set is still
    // worth having, and the store keeps whatever was there before.
    if (download(id, obj["url"] | "")) ++written;
  }

  // Written even after a partial run: the names that did arrive are still
  // worth labelling, and an entry for a screen that failed is harmless.
  writeNameIndex(doc);

  LOG_INF("DSET", "Synced %u/%u screens", static_cast<unsigned>(written), static_cast<unsigned>(seen));
  return written;
}

bool DashboardSet::syncOne(const std::string& id) {
  JsonDocument doc;
  if (!fetchManifest(doc)) return false;
  if (!ensureStoreDir()) return false;

  JsonArrayConst screens = doc["screens"].as<JsonArrayConst>();
  if (screens.isNull() || screens.size() == 0) return false;

  JsonObjectConst chosen;
  for (JsonObjectConst obj : screens) {
    if (id == (obj["id"] | "")) {
      chosen = obj;
      break;
    }
  }
  if (chosen.isNull()) {
    // The pinned screen is gone from the server. The manifest's own choice is
    // a better answer than refreshing nothing.
    const std::string activeId = doc["active"] | "";
    for (JsonObjectConst obj : screens) {
      if (activeId == (obj["id"] | "")) {
        chosen = obj;
        break;
      }
    }
    if (chosen.isNull()) chosen = screens[0];
    LOG_INF("DSET", "Screen '%s' not in manifest; refreshed '%s'", id.c_str(), chosen["id"] | "(first)");
  }

  const std::string chosenId = chosen["id"] | "";
  if (!isSafeId(chosenId)) return false;
  return download(chosenId, chosen["url"] | "");
}
