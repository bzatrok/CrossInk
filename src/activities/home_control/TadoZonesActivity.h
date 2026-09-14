#pragma once

#include <HomeControlHttp.h>
#include <Memory.h>
#include <TadoClient.h>
#include <TadoProtocol.h>

#include <memory>

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"
#include "util/ButtonNavigator.h"

/**
 * tado° heating zones: OAuth device-code sign-in on first use, then a tile
 * grid of zones with current and target temperatures plus "All off / Resume
 * all", and a per-zone screen to nudge the target or resume the schedule.
 * Runs inside the Home Control hub, which owns the Wi-Fi session. Network
 * buffers live from onEnter() to onExit().
 */
class TadoZonesActivity final : public Activity {
 public:
  explicit TadoZonesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class State : uint8_t {
    SIGNING_IN,   // refreshing the access token from the stored refresh token
    DEVICE_CODE,  // waiting for the user to approve on their phone
    LOADING_ZONES,
    ZONES,
    APPLYING,
    ZONE_DETAIL,
    ERROR,
  };
  enum class DetailAction : uint8_t { WARMER = 0, COOLER = 1, RESUME = 2 };
  static constexpr int kDetailActionCount = 3;
  enum class BulkAction : uint8_t { ALL_OFF = 0, RESUME_ALL = 1 };
  static constexpr int kBulkActionCount = 2;
  static constexpr size_t kLineLen = 32;

  bool allocateNetworkResources();
  void signIn();
  void startDeviceAuth();
  void deviceCodeTick();
  void loadZones();
  bool fetchZoneState(int index);
  void refreshZone(int index);
  void openZone(int index);
  void applyDetailAction(DetailAction action);
  void applyBulkAction(BulkAction action);
  void setState(State next);
  void failWith(TadoClient::Error error);
  void handleBack();
  void handleZonesInput();
  void handleDetailInput();
  void handleErrorInput();
  void formatZoneLines(int index);
  void renderMessage(const char* title, const char* body, bool showRetry);
  void renderDeviceCode();
  void renderZones();
  void renderDetail();

  State state = State::SIGNING_IN;
  int selectedZone = 0;
  int page = 0;
  ButtonNavigator buttonNavigator;
  ScreenTransitionRefresh screenTransitionRefresh;

  int32_t homeId = 0;
  tado::Zone zones[tado::kMaxZones];
  tado::ZoneState zoneStates[tado::kMaxZones];
  bool zoneStateKnown[tado::kMaxZones] = {};
  char zoneLine1[tado::kMaxZones][kLineLen];  // "Now 21.3°"
  char zoneLine2[tado::kMaxZones][kLineLen];  // "Target 19.5°" / "Manual 20.0°" / "Heating off"
  size_t zoneCount = 0;
  const char* errorMessage = nullptr;

  tado::DeviceCode deviceCode;
  unsigned long nextPollAtMs = 0;
  unsigned long pollIntervalMs = 0;
  unsigned long deviceCodeDeadlineMs = 0;
  unsigned long lastCountdownRenderMs = 0;

  HeapByteBuffer body;
  std::unique_ptr<homecontrol::HttpSession> session;
  std::unique_ptr<TadoClient> client;
};
