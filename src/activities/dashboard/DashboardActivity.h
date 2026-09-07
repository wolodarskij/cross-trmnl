#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "network/DashboardSet.h"

// Browser for the dashboard images held on the SD card (see DashboardSet).
//
// Opening it does no network work at all: it lists /dashboards and draws the
// selected screen straight from the card. Left/Right move between screens,
// Confirm is what reaches for the radio and downloads the set, Back leaves.
// That ordering is the point of the activity - a dashboard the user cannot
// look at without waiting for WiFi is a dashboard they stop opening.
//
// The legacy single-image sources (simple URL, TRMNL) still work here. They
// have one implicit screen, so the picker is inert and Confirm refreshes that
// one image instead of syncing a set.
//
// The DASHBOARD sleep-screen modes render the same selected image.
class DashboardActivity final : public Activity {
 public:
  explicit DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dashboard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return state == SYNCING; }
  bool isDashboardActivity() const override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { SHOWING, SYNCING, EMPTY };
  State state = SHOWING;

  // Screens on the card, sorted by id. Left empty for the legacy sources,
  // whose single image is addressed through DashboardImage instead.
  std::vector<DashboardSet::Screen> screens;
  int selected = 0;

  bool syncAttempted = false;
  bool syncSucceeded = false;
  // Set when the user actually moved the selection, so the state file is
  // written once on the way out rather than on every button press.
  bool selectionChanged = false;
  bool shouldTearDownWifiOnExit = false;

  bool usingScreenSet() const;
  void reloadScreens();
  void applySelection();
  void step(int delta);
  void runSync();
  void launchWifiSelection();
  std::string currentPath() const;
  void renderImage(const std::string& path);
};
