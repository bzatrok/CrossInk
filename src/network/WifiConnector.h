#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstdint>
#include <optional>

#include "WifiPowerSaveGuard.h"

// Headless STA connect for the dashboard timer wake, which cannot show the
// Wi-Fi selection UI. Tries the last-connected saved network first, then the
// rest. Blocking and bounded: each network gets PER_NETWORK_TIMEOUT_MS, the
// whole attempt TOTAL_BUDGET_MS. Modem power save stays off while connected.
class WifiConnector {
 public:
  static constexpr uint32_t PER_NETWORK_TIMEOUT_MS = 8000;
  static constexpr uint32_t TOTAL_BUDGET_MS = 15000;

  // True once connected with a valid IP. No-op when already connected.
  bool connectToSaved();
  // Disconnects and turns the radio off. Call before deep sleep.
  void disconnectAndOff();

 private:
  std::optional<WifiPowerSaveGuard> powerSaveGuard_;
};

#endif  // CROSSINK_APP_CAP_DASHBOARD
