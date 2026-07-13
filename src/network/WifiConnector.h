#pragma once
#include <cstdint>

/**
 * Headless STA connect for flows that cannot show the WiFi selection UI
 * (e.g. fetching the dashboard image while entering sleep). Tries the
 * last-connected network first, then the remaining saved credentials.
 *
 * Blocking: call only from contexts where a multi-second stall is acceptable.
 * The caller owns teardown (enterDeepSleep already turns WiFi off; interactive
 * callers should follow the silentRestart-on-exit convention).
 */
class WifiConnector {
 public:
  // Returns true once WiFi is connected with a valid IP. No-op if already
  // connected. perNetworkTimeoutMs bounds each credential attempt.
  static bool connectToSaved(uint32_t perNetworkTimeoutMs = 15000);
};
