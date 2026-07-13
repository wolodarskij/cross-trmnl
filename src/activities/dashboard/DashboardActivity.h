#pragma once

#include "activities/Activity.h"

// Manual entry point for the networked dashboard image (see DashboardImage).
// Connects WiFi (via the normal selection flow when needed), fetches the
// configured URL, and shows the image full-screen. Confirm re-fetches,
// Back leaves. The DASHBOARD sleep-screen mode reuses the same cache.
class DashboardActivity final : public Activity {
 public:
  explicit DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dashboard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return state == FETCHING; }
  bool isDashboardActivity() const override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { FETCHING, SHOWING, FAILED };
  State state = FETCHING;
  bool fetchSucceeded = false;
  bool shouldTearDownWifiOnExit = false;

  void runFetch();
  void launchWifiSelection();
};
