// Ported from cross-trmnl (https://github.com/wolodarskij/cross-trmnl),
// src/network/WifiConnector.cpp. MIT License, Copyright (c) 2025 Dave Allie.
// Changes: no BLE input stop, 8 s per network within a 15 s total budget,
// a held WifiPowerSaveGuard, and disconnectAndOff().

#include "WifiConnector.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <Logging.h>
#include <WiFi.h>

#include <string>

#include "WifiCredentialStore.h"

namespace {

bool isConnected() { return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0); }

// Mirrors WifiSelectionActivity::attemptConnection() so headless connects
// behave like interactive ones (hostname, NVS suppression).
void beginConnection(const WifiCredential& cred) {
  WiFi.persistent(false);  // Credentials live in WifiCredentialStore; suppress SDK NVS auto-connect
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);  // Abort any SDK auto-connect and clear the NVS-saved SSID
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

bool waitForConnection(const uint32_t timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (isConnected()) return true;
    delay(250);
  }
  return false;
}

}  // namespace

bool WifiConnector::connectToSaved() {
  if (isConnected()) return true;

  const size_t credentialCount = WIFI_STORE.getCredentialCount();
  if (credentialCount == 0) {
    LOG_INF("WFC", "No saved WiFi credentials");
    return false;
  }

  const unsigned long start = millis();
  auto attempt = [&](const WifiCredential& cred) {
    const uint32_t elapsed = millis() - start;
    if (elapsed >= TOTAL_BUDGET_MS) return false;
    const uint32_t remaining = TOTAL_BUDGET_MS - elapsed;
    LOG_DBG("WFC", "Trying %s", cred.ssid.c_str());
    beginConnection(cred);
    // The radio is up only after WiFi.mode(); the guard needs a started driver.
    if (!powerSaveGuard_) powerSaveGuard_.emplace();
    return waitForConnection(remaining < PER_NETWORK_TIMEOUT_MS ? remaining : PER_NETWORK_TIMEOUT_MS);
  };

  // Last-connected network first: most likely in range.
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    if (const auto lastCred = WIFI_STORE.findCredential(lastSsid)) {
      if (attempt(*lastCred)) {
        LOG_INF("WFC", "Connected to %s", lastCred->ssid.c_str());
        return true;
      }
    }
  }

  for (size_t i = 0; i < credentialCount; i++) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == lastSsid) continue;
    if (attempt(*cred)) {
      WIFI_STORE.setLastConnectedSsid(cred->ssid);
      LOG_INF("WFC", "Connected to %s", cred->ssid.c_str());
      return true;
    }
    if (millis() - start >= TOTAL_BUDGET_MS) break;
  }

  LOG_INF("WFC", "No saved network reachable within %u ms", static_cast<unsigned>(TOTAL_BUDGET_MS));
  WiFi.disconnect();
  return false;
}

void WifiConnector::disconnectAndOff() {
  // Restore the previous power-save mode while the driver still runs.
  powerSaveGuard_.reset();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

#endif  // CROSSINK_APP_CAP_DASHBOARD
