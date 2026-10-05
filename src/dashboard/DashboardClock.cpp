#include "DashboardClock.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <HalClock.h>

#include <cstdio>

#include "CrossPointSettings.h"

namespace dashboard::clock {

namespace {
// Howard Hinnant's days_from_civil: days since 1970-01-01 for a proleptic Gregorian date.
int32_t daysFromCivil(int32_t year, const uint32_t month, const uint32_t day) {
  year -= month <= 2 ? 1 : 0;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yearOfEra = static_cast<uint32_t>(year - era * 400);
  const uint32_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const uint32_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int32_t>(dayOfEra) - 719468;
}

int32_t utcOffsetSeconds() {
  const int offsetQuarterHours = static_cast<int>(SETTINGS.clockUtcOffsetQ > 104 ? 104 : SETTINGS.clockUtcOffsetQ) - 48;
  return offsetQuarterHours * 15 * 60;
}
}  // namespace

bool nowUtc(uint32_t& epochSeconds) {
  if (!halClock.isAvailable()) return false;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0;
  if (!halClock.getDateTime(year, month, day, hour, minute) || year < MIN_VALID_YEAR) return false;
  const int32_t days = daysFromCivil(year, month, day);
  epochSeconds = static_cast<uint32_t>(days) * 86400u + static_cast<uint32_t>(hour) * 3600u + minute * 60u;
  return true;
}

bool localTime(uint8_t& hour, uint8_t& minute) {
  uint32_t epoch = 0;
  if (!nowUtc(epoch)) return false;
  const uint32_t local = static_cast<uint32_t>(static_cast<int64_t>(epoch) + utcOffsetSeconds());
  hour = static_cast<uint8_t>((local / 3600) % 24);
  minute = static_cast<uint8_t>((local / 60) % 60);
  return true;
}

void formatLocalTime(const uint32_t epochSeconds, char* buf, const size_t bufSize) {
  if (bufSize == 0) return;
  if (epochSeconds == 0) {
    buf[0] = '\0';
    return;
  }
  const uint32_t local = static_cast<uint32_t>(static_cast<int64_t>(epochSeconds) + utcOffsetSeconds());
  snprintf(buf, bufSize, "%02u:%02u", static_cast<unsigned>((local / 3600) % 24),
           static_cast<unsigned>((local / 60) % 60));
}

}  // namespace dashboard::clock

#endif  // CROSSINK_APP_CAP_DASHBOARD
