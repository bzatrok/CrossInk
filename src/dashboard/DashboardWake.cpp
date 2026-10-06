#include "DashboardWake.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include <cstdint>
#include <cstring>

#ifndef SIMULATOR
#include <esp_timer.h>
#endif

#include "CrossPointSettings.h"
#include "DashboardClock.h"
#include "DashboardImageStore.h"
#include "DashboardPolicy.h"
#include "DashboardRender.h"
#include "DashboardSleep.h"
#include "DashboardSource.h"
#include "DashboardState.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "network/WifiConnector.h"

namespace DashboardWake {

namespace {
constexpr uint32_t NTP_RESYNC_SECONDS = 24 * 60 * 60;

#ifndef SIMULATOR
// Read by the backstop callback on the esp_timer task.
volatile uint32_t backstopTimerSeconds = 15 * 60;

void onBackstop(void*) {
  LOG_ERR("DSH", "Wake exceeded %us; forcing deep sleep", static_cast<unsigned>(BACKSTOP_SECONDS));
  // The HAL sleep path, not a bare esp_deep_sleep_start(): it keeps the power
  // latches held and arms the power-button wake. It touches no SD or display.
  powerManager.startDeepSleep(gpio, backstopTimerSeconds);
}
#endif

class Backstop {
 public:
  explicit Backstop(const uint32_t intervalSeconds) {
#ifndef SIMULATOR
    backstopTimerSeconds = intervalSeconds;
    const esp_timer_create_args_t args = {.callback = &onBackstop,
                                          .arg = nullptr,
                                          .dispatch_method = ESP_TIMER_TASK,
                                          .name = "dash_backstop",
                                          .skip_unhandled_events = true};
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_once(timer_, static_cast<uint64_t>(BACKSTOP_SECONDS) * 1000000ULL) != ESP_OK) {
      LOG_ERR("DSH", "Could not start the wake backstop");
    }
#else
    (void)intervalSeconds;
#endif
  }
  ~Backstop() {
#ifndef SIMULATOR
    if (timer_) {
      esp_timer_stop(timer_);
      esp_timer_delete(timer_);
    }
#endif
  }
  Backstop(const Backstop&) = delete;
  Backstop& operator=(const Backstop&) = delete;

 private:
#ifndef SIMULATOR
  esp_timer_handle_t timer_ = nullptr;
#endif
};

const char* screenName(const dashboard::Screen screen) {
  switch (screen) {
    case dashboard::Screen::Disabled:
      return "disabled";
    case dashboard::Screen::Dashboard:
      return "dashboard";
    case dashboard::Screen::FallbackQuiet:
      return "quiet";
    case dashboard::Screen::FallbackBattery:
      return "battery";
    case dashboard::Screen::FallbackFailures:
      return "failures";
  }
  return "?";
}

const char* fetchName(const DashboardFetch kind) {
  switch (kind) {
    case DashboardFetch::Updated:
      return "updated";
    case DashboardFetch::Unchanged:
      return "unchanged";
    case DashboardFetch::Failed:
      return "failed";
  }
  return "?";
}

// The regular sleep screen. DashboardSleep's onEnter hook returns false for
// fallback states, so SleepActivity draws the configured Sleep Screen.
void drawFallback(GfxRenderer& renderer, MappedInputManager& mappedInput, dashboard::DashboardState& state,
                  const dashboard::Screen screen) {
  state.save();  // the hook re-reads the state and must see this wake's failure count
  SleepActivity fallback(renderer, mappedInput, false);
  fallback.onEnter();
  DashboardImageStore::saveFrame(renderer.getFrameBuffer(), renderer.getBufferSize());
  state.screenShows = DashboardSleep::shownForFallback(screen);
  state.fastRefreshesSinceHalf = 0;
}

void maybeSyncClock(dashboard::DashboardState& state) {
  uint32_t now = 0;
  const bool clockValid = dashboard::clock::nowUtc(now);
  if (clockValid && state.lastNtpSyncUtc != 0 && now - state.lastNtpSyncUtc < NTP_RESYNC_SECONDS) return;
  if (!halClock.isAvailable() || !halClock.syncFromNTP()) {
    LOG_ERR("DSH", "NTP sync failed");
    return;
  }
  if (dashboard::clock::nowUtc(now)) state.lastNtpSyncUtc = now;
}
}  // namespace

DashboardSleep::SleepPlan run(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*setupDisplay)()) {
  const unsigned long startMs = millis();
  const uint32_t intervalSeconds =
      static_cast<uint32_t>(SETTINGS.dashboardInterval == 0 ? 1 : SETTINGS.dashboardInterval) * 60;
  Backstop backstop(intervalSeconds);

  setupDisplay();

  // Put the frame that is on the glass back into the framebuffer and the
  // controller's old-image plane, so a FAST refresh only changes what differs.
  bool frameRestored = false;
  if (DashboardImageStore::loadFrame(renderer.getFrameBuffer(), renderer.getBufferSize())) {
#ifndef SIMULATOR
    frameRestored = display.restoreVisibleFrame();
#endif
  }

  dashboard::DashboardState state;
  state.load();
  const dashboard::PolicyResult policy = DashboardSleep::evaluateNow(state);
  const char* fetchResult = "-";
  const char* refreshMode = "none";

  // The retry-limit fallback keeps fetching at the interval and returns on the first success.
  const bool fetches =
      policy.screen == dashboard::Screen::Dashboard || policy.screen == dashboard::Screen::FallbackFailures;
  if (!fetches) {
    // Quiet hours or battery floor: draw the fallback once per kind, then leave the glass alone.
    if (DashboardSleep::shownForFallback(policy.screen) != state.screenShows) {
      drawFallback(renderer, mappedInput, state, policy.screen);
      refreshMode = "fallback";
    }
    state.save();
  } else {
    DashboardSource& source = selectDashboardSource();
    WifiConnector wifi;
    bool online = !source.needsWifi();
    if (!online) {
      online = wifi.connectToSaved();
      if (online) maybeSyncClock(state);
    }
    const DashboardFetchResult fetch =
        online ? source.fetch(state) : makeDashboardFetchResult(DashboardFetch::Failed, "no wifi");
    fetchResult = fetchName(fetch.kind);

    uint32_t now = 0;
    const bool clockValid = dashboard::clock::nowUtc(now);
    bool draw = false;
    const char* banner = nullptr;
    char bannerText[48];
    switch (fetch.kind) {
      case DashboardFetch::Updated:
      case DashboardFetch::Unchanged:
        state.consecutiveFailures = 0;
        state.firstFailureUtc = 0;
        state.lastReason[0] = '\0';
        if (clockValid) state.lastSuccessUtc = now;
        // Unchanged redraws only to remove a banner or replace a fallback screen.
        draw = fetch.kind == DashboardFetch::Updated || state.screenShows != dashboard::Shown::Dashboard;
        break;
      case DashboardFetch::Failed:
        if (state.consecutiveFailures < UINT8_MAX) ++state.consecutiveFailures;
        if (state.consecutiveFailures == 1 && clockValid) state.firstFailureUtc = now;
        strncpy(state.lastReason, fetch.reason, sizeof(state.lastReason) - 1);
        state.lastReason[sizeof(state.lastReason) - 1] = '\0';
        if (state.consecutiveFailures >= SETTINGS.dashboardRetries) {
          if (state.screenShows != dashboard::Shown::FallbackFailures) {
            drawFallback(renderer, mappedInput, state, dashboard::Screen::FallbackFailures);
            refreshMode = "fallback";
          }
        } else if (state.screenShows != dashboard::Shown::Banner) {
          DashboardRender::formatBanner(state, bannerText, sizeof(bannerText));
          banner = bannerText;
          draw = true;
        }
        break;
    }

    if (draw) {
      const HalDisplay::RefreshMode mode = DashboardRender::chooseRefreshMode(state, frameRestored);
      if (DashboardRender::draw(renderer, banner, mode)) {
        state.screenShows = banner ? dashboard::Shown::Banner : dashboard::Shown::Dashboard;
        refreshMode = mode == HalDisplay::FAST_REFRESH ? "fast" : "half";
      } else {
        refreshMode = "draw-failed";
      }
    }
    state.save();
    wifi.disconnectAndOff();
  }

  // Recompute: this wake may have changed the failure count.
  const dashboard::PolicyResult next = DashboardSleep::evaluateNow(state);
  LOG_INF("DSH", "Timer wake: policy=%s fetch=%s refresh=%s next=%us keyLight=%d awake=%lums",
          screenName(policy.screen), fetchResult, refreshMode, static_cast<unsigned>(next.timerSeconds),
          next.keyLight ? 1 : 0, static_cast<unsigned long>(millis() - startMs));
  return {next.timerSeconds, next.keyLight};
}

}  // namespace DashboardWake

#endif  // CROSSINK_APP_CAP_DASHBOARD
