#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstddef>
#include <cstdint>

// Wall-clock helpers for Dashboard sleep. The RTC holds UTC; local time applies
// SETTINGS.clockUtcOffsetQ. HalClock has no epoch API, so this converts its
// calendar fields.
namespace dashboard::clock {

// The RTC is treated as unset before this year (it boots at 2000 without NTP).
constexpr uint16_t MIN_VALID_YEAR = 2026;

// UTC seconds since 1970. False without an RTC or before MIN_VALID_YEAR.
bool nowUtc(uint32_t& epochSeconds);

// Local hour and minute. False when nowUtc() is false.
bool localTime(uint8_t& hour, uint8_t& minute);

// "HH:MM" in local time for a UTC epoch (needs >= 6 bytes). Empty for 0.
void formatLocalTime(uint32_t epochSeconds, char* buf, size_t bufSize);

}  // namespace dashboard::clock

#endif  // CROSSINK_APP_CAP_DASHBOARD
