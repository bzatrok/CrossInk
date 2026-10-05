#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstddef>
#include <cstdint>

// Dashboard sleep state, persisted as /.crosspoint/dashboard/state.json.
// Kept on SD rather than in RTC memory: it survives power loss and there is
// one store to debug.
namespace dashboard {

// What is on the glass after the last sleep entry or timer wake.
enum class Shown : uint8_t {
  Nothing,           // no dashboard frame drawn yet
  Dashboard,         // the dashboard image, no banner
  Banner,            // the dashboard image with the "not updated" banner
  FallbackQuiet,     // the regular sleep screen, for quiet hours
  FallbackBattery,   // the regular sleep screen, for the battery floor
  FallbackFailures,  // the regular sleep screen, after the retry limit
};

struct DashboardState {
  uint32_t lastSuccessUtc = 0;   // epoch seconds of the last Updated/Unchanged fetch
  uint32_t firstFailureUtc = 0;  // epoch seconds of the first failure in the current run
  uint32_t lastNtpSyncUtc = 0;
  char lastFilename[96] = "";  // source-defined identity of current.bmp
  char lastReason[24] = "";    // reason of the last failure
  uint8_t consecutiveFailures = 0;
  Shown screenShows = Shown::Nothing;
  uint8_t fastRefreshesSinceHalf = 0;
  uint16_t localFolderIndex = 0;  // LocalFolderSource position of lastFilename

  // Missing or corrupt file -> defaults, logged.
  void load();
  bool save() const;

  bool showsFallback() const {
    return screenShows == Shown::FallbackQuiet || screenShows == Shown::FallbackBattery ||
           screenShows == Shown::FallbackFailures;
  }
};

// Settings > Dashboard status line, from the saved state.
void formatStatusLine(const DashboardState& state, char* buf, size_t bufSize);

}  // namespace dashboard

#endif  // CROSSINK_APP_CAP_DASHBOARD
