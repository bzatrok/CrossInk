#pragma once

#include <cstddef>
#include <cstdint>

#include "HomeControlHttp.h"
#include "HueProtocol.h"

// Talks to the Philips Hue bridge stored in HOME_CONTROL_STORE. Owns no heap:
// the caller passes the response buffer (PSRAM on x4-pro) and the HTTP session.
class HueClient {
 public:
  enum class Error : uint8_t {
    Ok,
    NoBridge,              // no bridge address configured
    NotPaired,             // no application key
    LinkButtonNotPressed,  // pairing in progress, retry
    Unauthorized,          // stored key rejected (403); caller should clear it and re-pair
    LowMemory,
    Network,
    BadResponse,
    Http,  // other non-2xx, see lastHttpStatus()
  };

  HueClient(homecontrol::HttpSession& session, uint8_t* bodyBuf, size_t bodyCap);

  // One pairing attempt against the stored bridge address. On Ok the key is
  // written to the store (not yet saved to SD; the caller decides when).
  Error pair();
  Error listRooms(hue::Room* out, size_t cap, size_t& count);
  // Every grouped_light on the bridge in one request: room states plus the
  // bridge_home group that switches all lights at once.
  Error listGroupedLights(hue::GroupedLight* out, size_t cap, size_t& count);
  // Last-recalled scene per room, in the order of rooms. The scene list can be
  // large; on BodyTooLarge the caller should fall back to plain on/off.
  Error listLastScenes(const hue::Room* rooms, size_t roomCount, hue::RoomScene* out);
  Error recallScene(const char* sceneId);
  Error getRoomState(const char* groupedLightId, hue::RoomState& out);
  Error setOn(const char* groupedLightId, bool on);
  Error setBrightness(const char* groupedLightId, uint8_t percent, bool alsoOn);

  int lastHttpStatus() const { return lastStatus; }
  static const char* errorName(Error error);  // for logs only

 private:
  Error call(const char* method, const char* path, const char* body, size_t bodyLen);
  Error mapStatus(int status) const;

  homecontrol::HttpSession& session;
  homecontrol::HttpResponse response;
  int lastStatus = 0;
  char urlBuf[192];
  char bodyBuf[96];
};
