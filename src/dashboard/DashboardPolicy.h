#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstdint>

// Pure decision logic for Dashboard sleep: which screen a sleep or timer wake
// should show, and when the next timer wake is due. No Arduino or HAL
// includes, so the native test suite covers it directly.
namespace dashboard {

enum class Screen : uint8_t {
  Disabled,          // Dashboard sleep is off: sleep exactly as without the feature.
  Dashboard,         // Show (and refresh) the dashboard image.
  FallbackQuiet,     // Inside quiet hours: the regular sleep screen until they end.
  FallbackBattery,   // Below the battery floor: the regular sleep screen, button wake only.
  FallbackFailures,  // Retry limit reached: the regular sleep screen, still retrying.
};

struct PolicyInput {
  bool enabled = false;
  bool clockValid = false;  // false without RTC time or before 2026; never counts as quiet
  uint8_t localHour = 0;
  uint8_t localMinute = 0;
  uint8_t batteryPercent = 100;
  uint8_t batteryFloorPercent = 0;  // 0 = off
  bool quietEnabled = false;
  uint8_t quietStartHour = 0;
  uint8_t quietEndHour = 0;
  uint8_t consecutiveFailures = 0;
  uint8_t retryLimit = 1;
  uint8_t intervalMinutes = 15;
};

struct PolicyResult {
  Screen screen;
  uint32_t timerSeconds;  // 0 = no timer wake (button wake only)
};

// True when hour falls inside [startHour, endHour), wrapping past midnight.
// start == end means "no quiet period".
bool isInQuietHours(uint8_t hour, uint8_t startHour, uint8_t endHour);

PolicyResult evaluatePolicy(const PolicyInput& input);

}  // namespace dashboard

#endif  // CROSSINK_APP_CAP_DASHBOARD
