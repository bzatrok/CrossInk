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
  bool keyLightAvailable = false;  // the board has a frontlight (decided by the caller)
  uint8_t consecutiveFailures = 0;
  uint8_t retryLimit = 1;
  uint8_t intervalMinutes = 15;
};

struct PolicyResult {
  Screen screen;
  uint32_t timerSeconds;  // 0 = no timer wake (button wake only)
  // Wait for the timer in light sleep with the side keys armed, so a press can
  // toggle the frontlight. Only while the dashboard still fetches.
  bool keyLight = false;
};

// True when hour falls inside [startHour, endHour), wrapping past midnight.
// start == end means "no quiet period".
bool isInQuietHours(uint8_t hour, uint8_t startHour, uint8_t endHour);

PolicyResult evaluatePolicy(const PolicyInput& input);

// How current.bmp maps onto the panel.
enum class DrawRotation : uint8_t {
  Portrait,      // a tall image, drawn as is
  LandscapeCcw,  // a wide image on a device standing landscape (native panel orientation)
  LandscapeCw,   // a wide image on a device standing portrait
};

// Tall images draw in portrait. A wide image on a portrait-standing device is
// a portrait layout the server rotated 90 degrees clockwise (Terminus Model
// rotation 90), so it draws rotated back to come out upright.
DrawRotation chooseDrawRotation(int imageWidth, int imageHeight, bool deviceStandsPortrait);

}  // namespace dashboard

#endif  // CROSSINK_APP_CAP_DASHBOARD
