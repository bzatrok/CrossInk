#include "DashboardSleep.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <CrossInkHalFrontlight.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "DashboardClock.h"
#include "DashboardImageStore.h"
#include "DashboardRender.h"

namespace DashboardSleep {

namespace {
bool refreshSoonRequested = false;
// What drawOnSleepEntry() decided, so planOnSleepEntry() acts on the same result.
bool entryEvaluated = false;
dashboard::PolicyResult entryResult{dashboard::Screen::Disabled, 0};
bool entryDrewDashboard = false;
}  // namespace

dashboard::PolicyResult evaluateNow(const dashboard::DashboardState& state) {
  dashboard::PolicyInput in;
  in.enabled = SETTINGS.dashboardEnabled != 0;
  in.clockValid = dashboard::clock::localTime(in.localHour, in.localMinute);
  in.batteryPercent = static_cast<uint8_t>(powerManager.getBatteryPercentage());
  in.batteryFloorPercent = SETTINGS.dashboardBatteryFloor;
  in.quietEnabled = SETTINGS.dashboardQuietEnabled != 0;
  in.quietStartHour = SETTINGS.dashboardQuietStart;
  in.quietEndHour = SETTINGS.dashboardQuietEnd;
  // The evening phase exists to light the frontlight on a side-key press.
  in.eveningEnabled = SETTINGS.dashboardEveningEnabled != 0 && Frontlight.present();
  in.eveningStartHour = SETTINGS.dashboardEveningStart;
  in.eveningEndHour = SETTINGS.dashboardEveningEnd;
  in.consecutiveFailures = state.consecutiveFailures;
  in.retryLimit = SETTINGS.dashboardRetries;
  in.intervalMinutes = SETTINGS.dashboardInterval;
  return dashboard::evaluatePolicy(in);
}

dashboard::Shown shownForFallback(const dashboard::Screen screen) {
  switch (screen) {
    case dashboard::Screen::FallbackQuiet:
      return dashboard::Shown::FallbackQuiet;
    case dashboard::Screen::FallbackBattery:
      return dashboard::Shown::FallbackBattery;
    case dashboard::Screen::FallbackFailures:
      return dashboard::Shown::FallbackFailures;
    default:
      return dashboard::Shown::Nothing;
  }
}

bool drawOnSleepEntry(GfxRenderer& renderer) {
  entryEvaluated = false;
  entryDrewDashboard = false;
  if (!SETTINGS.dashboardEnabled) return false;

  dashboard::DashboardState state;
  state.load();
  entryResult = evaluateNow(state);
  entryEvaluated = true;
  if (entryResult.screen != dashboard::Screen::Dashboard || !DashboardImageStore::hasCurrent()) return false;

  char banner[48];
  if (state.consecutiveFailures > 0) DashboardRender::formatBanner(state, banner, sizeof(banner));
  // The reader is on the glass, so there is no restored dashboard frame: HALF.
  display.setInverted(false);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (!DashboardRender::draw(renderer, state.consecutiveFailures > 0 ? banner : nullptr, HalDisplay::HALF_REFRESH)) {
    return false;
  }
  state.screenShows = state.consecutiveFailures > 0 ? dashboard::Shown::Banner : dashboard::Shown::Dashboard;
  state.fastRefreshesSinceHalf = 0;
  state.save();
  entryDrewDashboard = true;
  LOG_INF("DSH", "Sleep entry: dashboard drawn");
  return true;
}

SleepPlan planOnSleepEntry(GfxRenderer& renderer) {
  if (!SETTINGS.dashboardEnabled) {
    refreshSoonRequested = false;
    return {};
  }

  dashboard::DashboardState state;
  state.load();
  const dashboard::PolicyResult result = entryEvaluated ? entryResult : evaluateNow(state);
  entryEvaluated = false;

  if (!entryDrewDashboard) {
    // The regular sleep screen is on the glass. Keep it so the next wake can restore it.
    DashboardImageStore::saveFrame(renderer.getFrameBuffer(), renderer.getBufferSize());
    state.screenShows = shownForFallback(result.screen);
    state.fastRefreshesSinceHalf = 0;
    state.save();
  }

  const bool fetchSoon = refreshSoonRequested || result.screen == dashboard::Screen::Dashboard;
  refreshSoonRequested = false;
  SleepPlan plan;
  plan.timerSeconds = fetchSoon ? FIRST_FETCH_DELAY_SECONDS : result.timerSeconds;
  plan.evening = result.evening && !fetchSoon;
  LOG_INF("DSH", "Sleep entry: policy=%u timer=%us evening=%d", static_cast<unsigned>(result.screen),
          static_cast<unsigned>(plan.timerSeconds), plan.evening ? 1 : 0);
  return plan;
}

void requestRefreshSoon() { refreshSoonRequested = true; }

}  // namespace DashboardSleep

#endif  // CROSSINK_APP_CAP_DASHBOARD
