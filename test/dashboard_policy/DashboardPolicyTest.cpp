#include <gtest/gtest.h>

#include "dashboard/DashboardPolicy.h"

using dashboard::chooseDrawRotation;
using dashboard::DrawRotation;
using dashboard::evaluatePolicy;
using dashboard::isInQuietHours;
using dashboard::PolicyInput;
using dashboard::Screen;

namespace {

PolicyInput baseInput() {
  PolicyInput in;
  in.enabled = true;
  in.clockValid = true;
  in.localHour = 12;
  in.localMinute = 0;
  in.batteryPercent = 80;
  in.batteryFloorPercent = 15;
  in.quietEnabled = true;
  in.quietStartHour = 22;
  in.quietEndHour = 7;
  in.consecutiveFailures = 0;
  in.retryLimit = 3;
  in.intervalMinutes = 15;
  return in;
}

PolicyInput at(const uint8_t hour, const uint8_t minute) {
  PolicyInput in = baseInput();
  in.localHour = hour;
  in.localMinute = minute;
  return in;
}

}  // namespace

TEST(DashboardPolicy, DisabledSleepsWithoutTimer) {
  PolicyInput in = baseInput();
  in.enabled = false;
  const auto r = evaluatePolicy(in);
  EXPECT_EQ(r.screen, Screen::Disabled);
  EXPECT_EQ(r.timerSeconds, 0u);
}

TEST(DashboardPolicy, NormalShowsDashboardAtInterval) {
  const auto r = evaluatePolicy(baseInput());
  EXPECT_EQ(r.screen, Screen::Dashboard);
  EXPECT_EQ(r.timerSeconds, 15u * 60);
}

TEST(DashboardPolicy, QuietWrapsAcrossMidnight) {
  // 22 -> 7: 23:30 and 03:00 are inside; 07:00 and 21:59 are outside.
  auto r = evaluatePolicy(at(23, 30));
  EXPECT_EQ(r.screen, Screen::FallbackQuiet);
  EXPECT_EQ(r.timerSeconds, (30u + 7 * 60) * 60);

  r = evaluatePolicy(at(3, 0));
  EXPECT_EQ(r.screen, Screen::FallbackQuiet);
  EXPECT_EQ(r.timerSeconds, 4u * 60 * 60);

  EXPECT_EQ(evaluatePolicy(at(7, 0)).screen, Screen::Dashboard);
  EXPECT_EQ(evaluatePolicy(at(21, 59)).screen, Screen::Dashboard);
}

TEST(DashboardPolicy, QuietWithoutWrap) {
  EXPECT_TRUE(isInQuietHours(1, 1, 5));
  EXPECT_TRUE(isInQuietHours(4, 1, 5));
  EXPECT_FALSE(isInQuietHours(5, 1, 5));
  EXPECT_FALSE(isInQuietHours(0, 1, 5));
}

TEST(DashboardPolicy, QuietStartEqualsEndIsNoQuiet) {
  PolicyInput in = at(3, 0);
  in.quietStartHour = 5;
  in.quietEndHour = 5;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::Dashboard);
  EXPECT_FALSE(isInQuietHours(5, 5, 5));
}

TEST(DashboardPolicy, QuietOffIgnoresHours) {
  PolicyInput in = at(23, 30);
  in.quietEnabled = false;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::Dashboard);
}

TEST(DashboardPolicy, InvalidClockNeverCountsAsQuiet) {
  PolicyInput in = at(23, 30);
  in.clockValid = false;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::Dashboard);
}

TEST(DashboardPolicy, BatteryExactlyAtFloorIsNotBelow) {
  PolicyInput in = baseInput();
  in.batteryPercent = 15;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::Dashboard);

  in.batteryPercent = 14;
  const auto r = evaluatePolicy(in);
  EXPECT_EQ(r.screen, Screen::FallbackBattery);
  EXPECT_EQ(r.timerSeconds, 0u);
}

TEST(DashboardPolicy, BatteryFloorOff) {
  PolicyInput in = baseInput();
  in.batteryFloorPercent = 0;
  in.batteryPercent = 1;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::Dashboard);
}

TEST(DashboardPolicy, BatteryWinsOverQuiet) {
  PolicyInput in = at(23, 30);
  in.batteryPercent = 5;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::FallbackBattery);
}

static PolicyInput withKeyLight(const uint8_t hour, const uint8_t minute) {
  PolicyInput in = at(hour, minute);
  in.keyLightAvailable = true;
  return in;
}

TEST(DashboardPolicy, KeyLightWheneverTheDashboardShows) {
  const auto r = evaluatePolicy(withKeyLight(9, 0));
  EXPECT_EQ(r.screen, Screen::Dashboard);
  EXPECT_TRUE(r.keyLight);
  EXPECT_EQ(r.timerSeconds, 15u * 60);  // the refresh interval is unchanged
  EXPECT_TRUE(evaluatePolicy(withKeyLight(21, 59)).keyLight);
}

TEST(DashboardPolicy, KeyLightOffWithoutFrontlight) { EXPECT_FALSE(evaluatePolicy(at(9, 0)).keyLight); }

TEST(DashboardPolicy, KeyLightOffInQuietHours) {
  const auto r = evaluatePolicy(withKeyLight(23, 30));
  EXPECT_EQ(r.screen, Screen::FallbackQuiet);
  EXPECT_FALSE(r.keyLight);
}

TEST(DashboardPolicy, KeyLightOffBelowBatteryFloor) {
  PolicyInput in = withKeyLight(9, 0);
  in.batteryPercent = 5;
  const auto r = evaluatePolicy(in);
  EXPECT_EQ(r.screen, Screen::FallbackBattery);
  EXPECT_FALSE(r.keyLight);
}

TEST(DashboardPolicy, KeyLightContinuesWhileFailuresRetry) {
  PolicyInput in = withKeyLight(9, 0);
  in.consecutiveFailures = 3;
  const auto r = evaluatePolicy(in);
  EXPECT_EQ(r.screen, Screen::FallbackFailures);
  EXPECT_TRUE(r.keyLight);
}

TEST(DashboardPolicy, KeyLightNeedsNoClock) {
  PolicyInput in = withKeyLight(9, 0);
  in.clockValid = false;
  EXPECT_TRUE(evaluatePolicy(in).keyLight);
}

TEST(DashboardPolicy, FailuresBelowAndAtLimit) {
  PolicyInput in = baseInput();
  in.consecutiveFailures = 2;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::Dashboard);

  in.consecutiveFailures = 3;
  const auto r = evaluatePolicy(in);
  EXPECT_EQ(r.screen, Screen::FallbackFailures);
  EXPECT_EQ(r.timerSeconds, 15u * 60);  // keeps trying at the interval
}

TEST(DashboardPolicy, QuietWinsOverFailures) {
  PolicyInput in = at(23, 30);
  in.consecutiveFailures = 10;
  EXPECT_EQ(evaluatePolicy(in).screen, Screen::FallbackQuiet);
}

TEST(DashboardPolicy, EveryIntervalValue) {
  for (const uint8_t minutes : {1, 2, 3, 5, 10, 15, 30, 60}) {
    PolicyInput in = baseInput();
    in.intervalMinutes = minutes;
    EXPECT_EQ(evaluatePolicy(in).timerSeconds, static_cast<uint32_t>(minutes) * 60) << "interval " << int(minutes);
  }
}

TEST(DashboardPolicy, ZeroIntervalFallsBackToOneMinute) {
  PolicyInput in = baseInput();
  in.intervalMinutes = 0;
  EXPECT_EQ(evaluatePolicy(in).timerSeconds, 60u);
}

TEST(DashboardRotation, TallImageDrawsPortraitEitherWay) {
  EXPECT_EQ(chooseDrawRotation(480, 800, true), DrawRotation::Portrait);
  EXPECT_EQ(chooseDrawRotation(480, 800, false), DrawRotation::Portrait);
}

TEST(DashboardRotation, WideImageFollowsHowTheDeviceStands) {
  EXPECT_EQ(chooseDrawRotation(800, 480, true), DrawRotation::LandscapeCw);
  EXPECT_EQ(chooseDrawRotation(800, 480, false), DrawRotation::LandscapeCcw);
}
