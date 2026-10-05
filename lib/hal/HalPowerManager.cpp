#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

#include <cassert>

#include "HalFrontlight.h"
#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

namespace {
void disableWiFiBeforeDeepSleep() {
  const wifi_mode_t wifiMode = WiFi.getMode();
  if (wifiMode == WIFI_MODE_NULL) {
    return;
  }

  LOG_DBG("PWR", "Disabling WiFi before deep sleep (mode=%d)", static_cast<int>(wifiMode));
  if (wifiMode & WIFI_MODE_AP) {
    WiFi.softAPdisconnect(true);
  }
  if (wifiMode & WIFI_MODE_STA) {
    WiFi.disconnect(true);
  }
  delay(30);
  WiFi.mode(WIFI_OFF);
  delay(30);
}
}  // namespace

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

bool HalPowerManager::updateBatteryCalibration() {
  if (BoardConfig::ACTIVE.board != BoardConfig::Board::XteinkX3 &&
      BoardConfig::ACTIVE.board != BoardConfig::Board::XteinkX3Uc8279)
    return false;

  // The SDK performs one best-effort attempt per boot. False is terminal,
  // including I2C failure; an interrupted/failed load is retried next boot.
  const bool wasPending = batteryCalibrationPending;
  batteryCalibrationPending = BatteryMonitor::loadDesignCapacity();
  if (wasPending && !batteryCalibrationPending) {
    _batteryLastPollMs = 0;  // Sample the corrected gauge on the next battery read.
    LOG_INF("PWR", "X3 battery capacity check finished");
  }
  return batteryCalibrationPending;
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  if (modeMutex != nullptr) {
    xSemaphoreTake(modeMutex, portMAX_DELAY);
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      if (modeMutex != nullptr) {
        xSemaphoreGive(modeMutex);
      }
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      if (modeMutex != nullptr) {
        xSemaphoreGive(modeMutex);
      }
      return;
    }
    isLowPower = false;
  }

  if (modeMutex != nullptr) {
    xSemaphoreGive(modeMutex);
  }

  // Otherwise, no change needed
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio, const uint32_t timerWakeSeconds) const {
  // Once started, let the SDK exit configuration mode and seal the gauge.
  // Sleep runs on the main task after rendering has stopped. The SDK bounds
  // every wait; ordinary sleep has no delay once the startup check is done.
  while (batteryCalibrationPending && BatteryMonitor::loadDesignCapacity()) {
    delay(20);
  }
  disableWiFiBeforeDeepSleep();
  // Drive and hold the frontlight pads LOW: a light-sleep-capable (KEEP_ALIVE)
  // channel otherwise keeps drawing current through deep sleep.
  Frontlight.parkForDeepSleep();

#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  // Release every configured battery latch. BoardConfig owns the pin mapping;
  // the collision guard prevents a stale/mismatched profile from driving a
  // display or SD bus pin low and holding it through sleep.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || BoardConfig::latchConflictsWithBus(pin)) continue;
    const auto latch = static_cast<gpio_num_t>(pin);
    gpio_set_direction(latch, GPIO_MODE_OUTPUT);
    gpio_set_level(latch, 0);
    gpio_hold_en(latch);
  }
#else
  // Keep configured power latches asserted through deep sleep. The SDK isolates
  // GPIO pads before sleeping, so an unheld latch can float LOW once external
  // power is removed and turn a fast wake into a cold boot. This is deliberately
  // complementary to the C3 path above, where the battery latch must go LOW.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || BoardConfig::latchConflictsWithBus(pin)) continue;
    const auto latch = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(latch);
    gpio_set_direction(latch, GPIO_MODE_OUTPUT);
    gpio_set_level(latch, 1);
    gpio_hold_en(latch);
  }
#endif

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

  // The SDK convenience helper currently isolates every GPIO after arming the
  // wake source. On the ESP32-C3 that overwrites the power pin's sleep input
  // configuration, so short presses can be missed. Isolate first, then restore
  // and arm the board-configured power pin immediately before sleeping.
  freeink::PowerManager::waitForPowerButtonRelease();
  esp_sleep_config_gpio_isolate();
  freeink::PowerManager::armPowerButtonWakeup();
#ifndef SIMULATOR
  if (timerWakeSeconds > 0) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timerWakeSeconds) * 1000000ULL);
  }
#endif
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}

namespace {
// Active-LOW digital side keys that can wake light sleep (-1 = not wired).
// Up/Down are the X4 Pro's two physical keys (GPIO0 / GPIO7).
void sideButtonPins(int8_t (&pins)[2]) {
  pins[0] = BoardConfig::ACTIVE.input.up;
  pins[1] = BoardConfig::ACTIVE.input.down;
}

// Light-sleep GPIO wake is level-triggered: a held key would re-wake at once.
// Returns false when a key is still down after the timeout (stuck or held).
bool waitForSideButtonsRelease(const int8_t (&pins)[2]) {
  constexpr unsigned long RELEASE_TIMEOUT_MS = 5000;
  const unsigned long startMs = millis();
  for (;;) {
    bool held = false;
    for (const int8_t pin : pins) held = held || (pin >= 0 && digitalRead(pin) == LOW);
    if (!held) return true;
    if (millis() - startMs >= RELEASE_TIMEOUT_MS) return false;
    delay(20);
  }
}
}  // namespace

HalPowerManager::LightSleepWake HalPowerManager::startLightSleep(const uint32_t timerMs,
                                                                const bool wakeOnSideButtons) const {
  // No rail cut, pad isolation or pad holds: those are deep-sleep steps.
  disableWiFiBeforeDeepSleep();
  // The power wake is level-triggered: a held button would re-wake at once.
  freeink::PowerManager::waitForPowerButtonRelease();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  freeink::PowerManager::armPowerButtonWakeup();
  if (timerMs > 0) esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timerMs) * 1000ULL);

  int8_t pins[2];
  sideButtonPins(pins);
  bool armButtons = wakeOnSideButtons && (pins[0] >= 0 || pins[1] >= 0);
  if (armButtons && !waitForSideButtonsRelease(pins)) {
    LOG_ERR("PWR", "Side key held; light sleep without key wake");
    armButtons = false;
  }
  if (armButtons) {
    for (const int8_t pin : pins) {
      if (pin < 0) continue;
      const auto g = static_cast<gpio_num_t>(pin);
      // Keep the pad's active config (input + pull-up) through sleep instead of
      // the sleep-mode override, so the key can still pull the line LOW.
      gpio_sleep_sel_dis(g);
      gpio_wakeup_enable(g, GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
  }

#ifdef ENABLE_SERIAL_LOG
  logSerial.flush();
#endif
  const esp_err_t err = esp_light_sleep_start();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  // Restore the pads' normal interrupt type and sleep selection so polling
  // input and the deep-sleep pad isolation are unaffected.
  if (armButtons) {
    for (const int8_t pin : pins) {
      if (pin < 0) continue;
      const auto g = static_cast<gpio_num_t>(pin);
      gpio_wakeup_disable(g);
      gpio_sleep_sel_en(g);
    }
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  if (err != ESP_OK) {
    LOG_ERR("PWR", "Light sleep rejected (err=%d)", static_cast<int>(err));
    return LightSleepWake::Other;
  }
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return LightSleepWake::Timer;
    case ESP_SLEEP_WAKEUP_GPIO:
      return LightSleepWake::SideButton;
    case ESP_SLEEP_WAKEUP_EXT1:
      return LightSleepWake::PowerButton;
    default:
      return LightSleepWake::Other;
  }
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery;
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    _batteryLastPollMs = now;
    uint16_t percent = 0;
    if (!battery.readPercentageChecked(percent)) {
      return _batteryCachedPercent;
    }
    _batteryCachedPercent = percent;
    return _batteryCachedPercent;
  }

  // smooth the battery %.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * battery.readPercentage();
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + battery.readPercentage() * 10) / 10;
  }
  return _batteryCachedPercent / 10;
}

#if CROSSINK_BATTERY_DIAG_LOG
bool HalPowerManager::getBatteryDiagnostics(BatteryDiagnostics& out) const {
  // Function-local like getBatteryPercentage()'s: BoardConfig::ACTIVE is only
  // resolved once HalGPIO::begin() has run the X3/X4 probe, so a file-scope
  // instance could be constructed against an unresolved profile.
  static const BatteryMonitor battery;
  const BatteryMonitor::Status status = battery.readStatus();
  if (!status.supported) {
    LOG_ERR("PWR", "Battery diagnostics unsupported on this board");
    return false;
  }
  out.soc = status.percentage;
  out.millivolts = status.millivolts;
  out.charging = status.charging;
  out.socKnown = status.percentageKnown;
  out.millivoltsKnown = status.millivoltsKnown;
  out.chargingKnown = status.chargingKnown;
  return true;
}
#endif

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
