#include "WifiConnector.h"

#include <Logging.h>
#include <WiFi.h>

#include "WifiCredentialStore.h"

namespace {

// Mirrors WifiSelectionActivity::attemptConnection() so headless connects
// behave exactly like interactive ones (hostname, NVS suppression, etc.).
void beginConnection(const WifiCredential& cred) {
  WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore; suppress SDK NVS auto-connect
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);  // Abort any in-progress SDK auto-connect and clear NVS-saved SSID
  delay(100);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  const String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());

  if (!cred.password.empty()) {
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
  } else {
    WiFi.begin(cred.ssid.c_str());
  }
}

bool waitForConnection(uint32_t timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      return true;
    }
    delay(250);
  }
  return false;
}

}  // namespace

bool WifiConnector::connectToSaved(uint32_t perNetworkTimeoutMs) {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    return true;
  }

  const auto& credentials = WIFI_STORE.getCredentials();
  if (credentials.empty()) {
    LOG_INF("WFC", "No saved WiFi credentials");
    return false;
  }

  // Last-connected network first: it is the most likely to be in range and
  // avoids burning the timeout budget on stale entries.
  const std::string& lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (const auto* lastCred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid)) {
    LOG_DBG("WFC", "Trying last network: %s", lastCred->ssid.c_str());
    beginConnection(*lastCred);
    if (waitForConnection(perNetworkTimeoutMs)) {
      LOG_INF("WFC", "Connected to %s", lastCred->ssid.c_str());
      return true;
    }
  }

  for (const auto& cred : credentials) {
    if (cred.ssid == lastSsid) continue;  // already attempted above
    LOG_DBG("WFC", "Trying saved network: %s", cred.ssid.c_str());
    beginConnection(cred);
    if (waitForConnection(perNetworkTimeoutMs)) {
      WIFI_STORE.setLastConnectedSsid(cred.ssid);
      LOG_INF("WFC", "Connected to %s", cred.ssid.c_str());
      return true;
    }
  }

  LOG_INF("WFC", "No saved network reachable");
  WiFi.disconnect();
  return false;
}
