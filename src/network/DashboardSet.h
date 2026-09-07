#pragma once

#include <string>
#include <vector>

/**
 * Multi-screen dashboard support: a local store of BMPs on the SD card plus
 * the client for the server that fills it.
 *
 * The store is the source of truth, not the server. Everything the UI needs
 * (list the screens, pick one, render it) reads the card and never touches
 * the network, so the dashboard opens instantly and works with the radio off.
 * A sync is an explicit user action that adds files to the store.
 *
 * That split is also what makes a server optional: copy BMPs into
 * /dashboards yourself and they are indistinguishable from downloaded ones.
 *
 * Server contract: x4-dashboard-server's docs/screens-format.md.
 *   GET {base}/screens.json   -> {format, version, screens:[{id, name, url}]}
 *   GET {base}{screen.url}    -> the BMP, already panel-ready
 *
 * A sync never deletes. A screen removed on the server, and a file the user
 * copied in by hand, are the same thing from here: a BMP with no manifest
 * entry. Deleting one to tidy the other away is not a trade worth making, so
 * pruning is left to whoever manages the card.
 */
class DashboardSet {
 public:
  // User-visible on purpose, alongside /scripts: dropping a BMP in here is a
  // supported way to add a dashboard, not a workaround.
  static constexpr const char* kScreensDir = "/dashboards";

  // Bounds the scan and the manifest parse. Every screen costs an id, a name
  // and a vector slot; a server answering with thousands would otherwise size
  // the allocation for us.
  static constexpr size_t kMaxScreens = 32;

  // Matches the id length the server contract guarantees (<=48 chars).
  static constexpr size_t kMaxIdLen = 48;

  struct Screen {
    std::string id;    // filename stem, and the manifest id it came from
    std::string name;  // label for the picker; the id when nothing better exists
  };

  // Lists the store, sorted by id. Pure SD work - no network, safe to call
  // from onEnter. Clears out first; never partially fills on failure.
  static void scan(std::vector<Screen>& out);

  // Absolute path of a screen's BMP. Does not check that it exists.
  static std::string pathFor(const std::string& id);

  // True when a screen-set server address is configured.
  static bool isConfigured();

  // Fetches the manifest and downloads every screen it lists. WiFi must
  // already be connected. Individual screen failures are skipped rather than
  // aborting the run, so one broken screen cannot block the rest.
  // Returns the number of screens successfully written.
  static size_t syncAll();

  // Refreshes a single screen, for the sleep path where downloading the whole
  // set on every sleep would cost far more than the one image being shown.
  // Falls back to the manifest's active screen when id is empty or unknown.
  static bool syncOne(const std::string& id);
};
