#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstdint>

#include "DashboardPolicy.h"
#include "DashboardState.h"

class GfxRenderer;

// Dashboard sleep hooks on the normal sleep path (button, timeout, menu).
// On sleep entry the device draws the cached image and arms a 2 s timer, so
// the first fetch runs on the timer wake rather than with the reader on screen.
namespace DashboardSleep {

// What the deep-sleep tail does after a sleep entry or a timer wake.
struct SleepPlan {
  uint32_t timerSeconds = 0;  // 0 = button wake only
  bool keyLight = false;      // wait for the timer in light sleep (DashboardKeyLight)
};

// Seconds until the first fetch after the dashboard is drawn on sleep entry.
constexpr uint32_t FIRST_FETCH_DELAY_SECONDS = 2;

// Evaluates the policy for this moment (clock, battery, settings, state).
dashboard::PolicyResult evaluateNow(const dashboard::DashboardState& state);

// Maps a fallback policy screen to what the glass shows.
dashboard::Shown shownForFallback(dashboard::Screen screen);

// SleepActivity::onEnter hook. True when it drew current.bmp (HALF refresh,
// banner after a failure) and saved frame.bin. False otherwise; the regular
// sleep screen then draws and is the fallback.
bool drawOnSleepEntry(GfxRenderer& renderer);

// enterDeepSleep hook, after the sleep screen is on the glass. Saves frame.bin
// and the state when a fallback screen was drawn, and returns the timer wake
// (0 = button wake only). Returns an empty plan when Dashboard sleep is off.
// Sleep entry never waits in light sleep: the first fetch is seconds away.
SleepPlan planOnSleepEntry(GfxRenderer& renderer);

// Settings > Dashboard > Refresh now: the next sleep entry arms a 2 s timer.
void requestRefreshSoon();

}  // namespace DashboardSleep

#endif  // CROSSINK_APP_CAP_DASHBOARD
