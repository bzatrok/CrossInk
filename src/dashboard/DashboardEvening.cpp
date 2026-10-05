#include "DashboardEvening.h"

#if CROSSINK_APP_CAP_DASHBOARD && !defined(SIMULATOR)

#include <CrossInkHalFrontlight.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include "CrossPointSettings.h"

namespace DashboardEvening {

namespace {
using Wake = HalPowerManager::LightSleepWake;

constexpr unsigned long LIGHT_ON_MS = LIGHT_ON_SECONDS * 1000UL;
constexpr unsigned long PRESS_CONFIRM_MS = 30;  // a wake without a key still down after this is a glitch

// The key wake is level-triggered on a raw pad. Confirm through the debounced
// input path that a side key is really down before acting on it.
bool sideKeyConfirmed() {
  const unsigned long startMs = millis();
  while (millis() - startMs < PRESS_CONFIRM_MS) {
    gpio.update();
    delay(5);
  }
  return gpio.isPressed(HalGPIO::BTN_UP) || gpio.isPressed(HalGPIO::BTN_DOWN);
}

void setLight(const bool on) {
  if (on) Frontlight.setBrightness(SETTINGS.dashboardEveningBrightness);
  Frontlight.setOn(on);
}
}  // namespace

uint32_t waitForNextFetch(const uint32_t timerSeconds, const bool shortPressWakes) {
  const unsigned long startMs = millis();
  const unsigned long fetchDueMs = static_cast<unsigned long>(timerSeconds) * 1000UL;

  // The sleep-entry path can arrive with the reading light on.
  setLight(false);
  bool lightOn = false;
  unsigned long lightOnSinceMs = 0;
  LOG_INF("DSH", "Evening wait %us", static_cast<unsigned>(timerSeconds));

  for (;;) {
    const unsigned long now = millis();
    if (lightOn && now - lightOnSinceMs >= LIGHT_ON_MS) {
      setLight(false);
      lightOn = false;
    }
    const unsigned long elapsedMs = now - startMs;
    if (!lightOn && elapsedMs >= fetchDueMs) return FETCH_HANDOFF_SECONDS;
    const unsigned long sleepMs = lightOn ? LIGHT_ON_MS - (now - lightOnSinceMs) : fetchDueMs - elapsedMs;

    const Wake wake = powerManager.startLightSleep(sleepMs, true);

    switch (wake) {
      case Wake::Timer:
        break;
      case Wake::SideButton:
        if (!sideKeyConfirmed()) break;
        lightOn = !lightOn;
        setLight(lightOn);
        if (lightOn) lightOnSinceMs = millis();
        LOG_INF("DSH", "Evening: light %s", lightOn ? "on" : "off");
        break;
      case Wake::PowerButton:
        if (gpio.verifyPowerButtonWakeup(shortPressWakes, CrossPointSettings::POWER_BUTTON_LONG_PRESS_MS, millis())) {
          gpio.restartAsPowerButtonWake();
        }
        break;
      case Wake::Other: {
        // Light sleep failed or woke for an unknown reason. Deep-sleep the rest:
        // retrying here could spin awake for the whole interval.
        if (lightOn) setLight(false);
        const unsigned long leftMs = fetchDueMs > elapsedMs ? fetchDueMs - elapsedMs : 0;
        const uint32_t leftSeconds = static_cast<uint32_t>(leftMs / 1000UL);
        LOG_ERR("DSH", "Evening: light sleep failed; deep sleep for %us", static_cast<unsigned>(leftSeconds));
        return leftSeconds > FETCH_HANDOFF_SECONDS ? leftSeconds : FETCH_HANDOFF_SECONDS;
      }
    }
  }
}

}  // namespace DashboardEvening

#endif  // CROSSINK_APP_CAP_DASHBOARD && !SIMULATOR
