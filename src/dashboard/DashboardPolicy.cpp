#include "DashboardPolicy.h"

#if CROSSINK_APP_CAP_DASHBOARD

namespace dashboard {

namespace {
constexpr uint32_t MINUTES_PER_DAY = 24 * 60;

uint32_t intervalSeconds(const uint8_t intervalMinutes) {
  // A corrupt zero interval would wake the device in a tight loop; one minute is the floor.
  return static_cast<uint32_t>(intervalMinutes == 0 ? 1 : intervalMinutes) * 60;
}

uint32_t secondsUntilHour(const uint8_t hour, const uint8_t minute, const uint8_t targetHour) {
  const uint32_t now = static_cast<uint32_t>(hour) * 60 + minute;
  const uint32_t target = static_cast<uint32_t>(targetHour) * 60;
  const uint32_t minutes = (target + MINUTES_PER_DAY - now) % MINUTES_PER_DAY;
  return (minutes == 0 ? MINUTES_PER_DAY : minutes) * 60;
}
}  // namespace

bool isInQuietHours(const uint8_t hour, const uint8_t startHour, const uint8_t endHour) {
  if (startHour == endHour) return false;
  if (startHour < endHour) return hour >= startHour && hour < endHour;
  return hour >= startHour || hour < endHour;
}

PolicyResult evaluatePolicy(const PolicyInput& input) {
  if (!input.enabled) return {Screen::Disabled, 0};

  if (input.batteryFloorPercent != 0 && input.batteryPercent < input.batteryFloorPercent) {
    return {Screen::FallbackBattery, 0};
  }

  if (input.quietEnabled && input.clockValid &&
      isInQuietHours(input.localHour, input.quietStartHour, input.quietEndHour)) {
    return {Screen::FallbackQuiet, secondsUntilHour(input.localHour, input.localMinute, input.quietEndHour)};
  }

  if (input.consecutiveFailures >= input.retryLimit) {
    return {Screen::FallbackFailures, intervalSeconds(input.intervalMinutes)};
  }

  return {Screen::Dashboard, intervalSeconds(input.intervalMinutes)};
}

DrawRotation chooseDrawRotation(const int imageWidth, const int imageHeight, const bool deviceStandsPortrait) {
  if (imageWidth <= imageHeight) return DrawRotation::Portrait;
  return deviceStandsPortrait ? DrawRotation::LandscapeCw : DrawRotation::LandscapeCcw;
}

}  // namespace dashboard

#endif  // CROSSINK_APP_CAP_DASHBOARD
