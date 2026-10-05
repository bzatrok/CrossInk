#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstdint>

class GfxRenderer;
class MappedInputManager;

// Headless timer-wake path for Dashboard sleep. setup() calls run() instead of
// the normal boot when the wake cause is the RTC timer: no boot screen, no
// Home, no reader. It fetches, redraws only when needed, and returns the next
// timer wake in seconds for the caller's deep sleep.
namespace DashboardWake {

// Hard cap on one timer wake. When it fires, the device deep-sleeps with the
// interval timer whatever the main task is doing.
constexpr uint32_t BACKSTOP_SECONDS = 60;

// `setupDisplay` initialises the panel seamlessly (frame kept) and loads UI fonts.
uint32_t run(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*setupDisplay)());

}  // namespace DashboardWake

#endif  // CROSSINK_APP_CAP_DASHBOARD
