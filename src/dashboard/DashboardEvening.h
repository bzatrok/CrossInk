#pragma once

#if CROSSINK_APP_CAP_DASHBOARD && !defined(SIMULATOR)

#include <cstdint>

// Evening phase of Dashboard sleep. Between two refreshes the device waits in
// light sleep instead of deep sleep, so a side-key press can toggle the
// frontlight. Refreshes still run on the normal timer wake: when one is due,
// the caller deep-sleeps for FETCH_HANDOFF_SECONDS.
namespace DashboardEvening {

// The deep-sleep timer that hands a due refresh to the normal timer-wake path.
constexpr uint32_t FETCH_HANDOFF_SECONDS = 1;
// How long a side-key press keeps the light on. A refresh that falls due meanwhile
// waits for the light to go out.
constexpr uint32_t LIGHT_ON_SECONDS = 60;

// Call from the deep-sleep tail, after the panel, SD and tilt sensor sleep.
// Waits timerSeconds and returns the deep-sleep timer to use next:
// FETCH_HANDOFF_SECONDS, or the time still left when light sleep fails.
// A power press that qualifies (shortPressWakes, else a long hold) restarts into
// a normal wake and does not return.
uint32_t waitForNextFetch(uint32_t timerSeconds, bool shortPressWakes);

}  // namespace DashboardEvening

#endif  // CROSSINK_APP_CAP_DASHBOARD && !SIMULATOR
