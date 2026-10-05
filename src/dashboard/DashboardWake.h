#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstdint>

#include "DashboardSleep.h"

class GfxRenderer;
class MappedInputManager;

// Headless timer-wake path for Dashboard sleep. setup() calls run() instead of
// the normal boot when the wake cause is the RTC timer: no boot screen, no
// Home, no reader. It fetches, redraws only when needed, and returns the next
// timer wake for the caller's deep sleep, and whether to wait for it in the
// evening phase's light sleep.
namespace DashboardWake {

// Hard cap on one timer wake. When it fires, the device deep-sleeps with the
// interval timer whatever the main task is doing.
constexpr uint32_t BACKSTOP_SECONDS = 60;

// `setupDisplay` initialises the panel seamlessly (frame kept) and loads UI fonts.
DashboardSleep::SleepPlan run(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*setupDisplay)());

}  // namespace DashboardWake

#endif  // CROSSINK_APP_CAP_DASHBOARD
