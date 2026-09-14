#pragma once

#include <HomeControlHttp.h>
#include <HueClient.h>
#include <HueProtocol.h>
#include <Memory.h>

#include <memory>

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"
#include "util/ButtonNavigator.h"

/**
 * Philips Hue rooms: bridge discovery, link-button pairing, a tile grid of
 * rooms with their live state and "All on / All off", and a per-room screen
 * with on/off and brightness. Runs inside the Home Control hub, which owns
 * the Wi-Fi session. All network buffers are allocated in onEnter() and
 * released in onExit().
 */
class HueRoomsActivity final : public Activity {
 public:
  explicit HueRoomsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class State : uint8_t {
    SEARCHING,  // mDNS lookup for _hue._tcp
    NEED_IP,
    PAIRING,
    LOADING_ROOMS,
    ROOMS,
    APPLYING,  // bulk action in flight
    ROOM_DETAIL,
    ERROR,
  };
  enum class DetailAction : uint8_t { TOGGLE = 0, BRIGHTER = 1, DIMMER = 2 };
  static constexpr int kDetailActionCount = 3;
  enum class BulkAction : uint8_t { ALL_ON = 0, ALL_OFF = 1 };
  static constexpr int kBulkActionCount = 2;

  bool allocateNetworkResources();
  void discoverBridge();
  void askForBridgeIp();
  void startPairing();
  void pairingTick();
  void loadRooms();
  bool loadRoomStates();
  void loadRoomScenes();
  HueClient::Error turnRoomOn(int index);
  void openRoom(int index);
  void applyDetailAction(DetailAction action);
  void applyBulkAction(BulkAction action);
  void setState(State next);
  void failWith(HueClient::Error error);
  void handleBack();
  void handleDetailInput();
  void handleErrorInput();
  void handleRoomsInput();
  void formatRoomLine(int index, char* out, size_t cap) const;
  void renderMessage(const char* title, const char* body, bool showRetry);
  void renderRooms();
  void renderDetail();

  State state = State::SEARCHING;
  int selectedRoom = 0;
  int page = 0;
  ButtonNavigator buttonNavigator;
  ScreenTransitionRefresh screenTransitionRefresh;

  hue::Room rooms[hue::kMaxRooms];
  hue::RoomState roomStates[hue::kMaxRooms];
  bool roomStateKnown[hue::kMaxRooms] = {};
  hue::RoomScene roomScenes[hue::kMaxRooms];  // empty sceneId = plain on/off
  size_t roomCount = 0;
  char allLightsId[hue::kIdLen] = "";  // bridge_home grouped_light, empty if the bridge has none
  const char* errorMessage = nullptr;

  unsigned long nextPairAttemptAtMs = 0;
  unsigned long pairingDeadlineMs = 0;

  HeapByteBuffer body;
  std::unique_ptr<homecontrol::HttpSession> session;
  std::unique_ptr<HueClient> client;
};
