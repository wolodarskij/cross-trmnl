#pragma once

#include <string>
#include <vector>

#include "Features.h"
#include "activities/Activity.h"
#if CROSSPOINT_FEATURE_DASHBOARD
#include "network/DashboardSet.h"
#endif

// Browser for the dashboard images held on the SD card.
//
// Opening it does no network work at all: it lists /dashboards and draws the
// selected screen straight from the card. Left/Right move between screens,
// Confirm is what reaches for the radio and syncs, Back leaves. That ordering
// is the point of the activity - a dashboard the user cannot look at without
// waiting for WiFi is a dashboard they stop opening.
//
// With the x4-dashboard-server source the server decides the mode: a
// screens.json manifest fills /dashboards and the picker works; a server that
// only serves dashboard.bmp yields one implicit screen (the picker is inert)
// and Confirm refreshes that image. TRMNL always has one implicit screen.
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

#if CROSSPOINT_FEATURE_DASHBOARD
  // Screens on the card, sorted by id. Empty for TRMNL and for a single-image
  // server, whose one image is addressed through DashboardImage instead.
  std::vector<DashboardSet::Screen> screens;
#endif
  int selected = 0;

  bool syncAttempted = false;
  bool syncSucceeded = false;
  // Set when the user actually moved the selection, so the state file is
  // written once on the way out rather than on every button press.
  bool selectionChanged = false;
  bool shouldTearDownWifiOnExit = false;

  // True when the x4-dashboard-server source is active (the one with a
  // screen store); false for TRMNL.
  bool usingX4() const;
  int screenCount() const;
  void reloadScreens();
  void applySelection();
  void step(int delta);
  void runSync();
  void launchWifiSelection();
  std::string currentPath() const;
  void renderImage(const std::string& path);
};
