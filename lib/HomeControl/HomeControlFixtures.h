#pragma once

// Canned Hue and tado responses for the native simulator, so the Home Control
// screens can be driven end to end without hardware or accounts. Only included
// from HomeControlHttp.cpp under SIMULATOR.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HomeControlHttp.h"

namespace homecontrol::fixtures {

namespace detail {

inline bool contains(const char* haystack, const char* needle) {
  return haystack != nullptr && std::strstr(haystack, needle) != nullptr;
}

inline int reply(HttpResponse& out, const int status, const char* body) {
  const size_t len = std::strlen(body);
  const size_t take = len < out.cap - 1 ? len : out.cap - 1;
  std::memcpy(out.buf, body, take);
  out.len = take;
  out.truncated = take < len;
  return status;
}

constexpr const char* kPairLinkNotPressed =
    R"([{"error":{"type":101,"address":"","description":"link button not pressed"}}])";
constexpr const char* kPairSuccess =
    R"([{"success":{"username":"sim-app-key-0000","clientkey":"sim-client-key"}}])";
constexpr const char* kRooms = R"({"errors":[],"data":[
  {"id":"room-living","type":"room","children":[],"services":[{"rid":"gl-living","rtype":"grouped_light"}],
   "metadata":{"name":"Living room","archetype":"living_room"}},
  {"id":"room-kitchen","type":"room","children":[],"services":[{"rid":"gl-kitchen","rtype":"grouped_light"}],
   "metadata":{"name":"Kitchen","archetype":"kitchen"}},
  {"id":"room-bedroom","type":"room","children":[],"services":[{"rid":"gl-bedroom","rtype":"grouped_light"}],
   "metadata":{"name":"Bedroom","archetype":"bedroom"}}]})";
// Simulated light state per grouped_light id, so PUTs are visible on the next GET.
struct SimLight {
  const char* rid;
  bool on;
  int brightness;
};
inline SimLight* simLights() {
  static SimLight lights[] = {{"gl-living", true, 60}, {"gl-kitchen", false, 30}, {"gl-bedroom", false, 100}};
  return lights;
}
constexpr size_t kSimLightCount = 3;

inline SimLight* findSimLight(const char* url) {
  for (size_t i = 0; i < kSimLightCount; ++i) {
    if (contains(url, simLights()[i].rid)) return &simLights()[i];
  }
  return nullptr;
}

// Applies a grouped_light PUT body to the simulated state.
inline void applyLightBody(SimLight& light, const char* body) {
  if (contains(body, "\"on\":true")) light.on = true;
  if (contains(body, "\"on\":false")) light.on = false;
  const char* dim = body != nullptr ? std::strstr(body, "\"brightness\":") : nullptr;
  if (dim != nullptr) light.brightness = std::atoi(dim + 13);
}

// The bridge_home group: a PUT here reaches every simulated light.
constexpr const char* kAllLightsRid = "gl-all";

inline void applyLightBodyToAll(const char* body) {
  for (size_t i = 0; i < kSimLightCount; ++i) applyLightBody(simLights()[i], body);
}

inline int replyGroupedLightList(HttpResponse& out) {
  char body[640];
  size_t pos = static_cast<size_t>(std::snprintf(body, sizeof(body), R"({"errors":[],"data":[)"));
  bool anyOn = false;
  for (size_t i = 0; i < kSimLightCount; ++i) {
    const SimLight& light = simLights()[i];
    anyOn = anyOn || light.on;
    pos += static_cast<size_t>(std::snprintf(
        body + pos, sizeof(body) - pos,
        R"({"id":"%s","type":"grouped_light","owner":{"rid":"room-%u","rtype":"room"},"on":{"on":%s},"dimming":{"brightness":%d.0}},)",
        light.rid, static_cast<unsigned>(i), light.on ? "true" : "false", light.brightness));
  }
  std::snprintf(body + pos, sizeof(body) - pos,
                R"({"id":"%s","type":"grouped_light","owner":{"rid":"bh-1","rtype":"bridge_home"},"on":{"on":%s},"dimming":{"brightness":50.0}}]})",
                kAllLightsRid, anyOn ? "true" : "false");
  return reply(out, 200, body);
}

inline int replyLightState(HttpResponse& out, const SimLight& light) {
  char body[160];
  std::snprintf(body, sizeof(body), R"({"errors":[],"data":[{"id":"%s","on":{"on":%s},"dimming":{"brightness":%d.0}}]})",
                light.rid, light.on ? "true" : "false", light.brightness);
  return reply(out, 200, body);
}
constexpr const char* kPutOk = R"({"errors":[],"data":[{"rid":"gl","rtype":"grouped_light"}]})";

constexpr const char* kDeviceAuthorize =
    R"({"device_code":"sim-device-code","expires_in":300,"interval":1,"user_code":"SIM1-CODE",
        "verification_uri":"https://login.tado.com/oauth2/device",
        "verification_uri_complete":"https://login.tado.com/oauth2/device?user_code=SIM1-CODE"})";
constexpr const char* kTokenPending = R"({"error":"authorization_pending","error_description":"pending"})";
constexpr const char* kTokenOk =
    R"({"access_token":"sim-access-token","expires_in":599,"refresh_token":"sim-refresh-token","scope":"offline_access","token_type":"Bearer"})";
constexpr const char* kMe = R"({"name":"Sim","email":"sim@example.org","homes":[{"id":424242,"name":"Sim home"}]})";
constexpr const char* kZones = R"([
  {"id":1,"name":"Living room","type":"HEATING"},
  {"id":2,"name":"Hot water","type":"HOT_WATER"},
  {"id":3,"name":"Office","type":"HEATING"}])";
// Simulated heating state per zone id: schedule target 19.5°, manual overlay when set.
struct SimZone {
  int id;
  float inside;
  float scheduleTarget;
  bool overlay;
  float overlayTarget;
  bool overlayOff;  // manual "heating off" overlay
};
inline SimZone* simZones() {
  static SimZone zones[] = {{1, 21.3f, 19.5f, false, 0.0f, false}, {3, 18.2f, 20.0f, false, 0.0f, false}};
  return zones;
}
constexpr size_t kSimZoneCount = 2;

inline SimZone* findSimZone(const char* url) {
  const char* marker = std::strstr(url, "/zones/");
  if (marker == nullptr) return nullptr;
  const int id = std::atoi(marker + 7);
  for (size_t i = 0; i < kSimZoneCount; ++i) {
    if (simZones()[i].id == id) return &simZones()[i];
  }
  return nullptr;
}

inline int replyZoneState(HttpResponse& out, const SimZone& zone) {
  const float target = zone.overlay ? zone.overlayTarget : zone.scheduleTarget;
  char setting[96];
  if (zone.overlay && zone.overlayOff) {
    std::snprintf(setting, sizeof(setting), R"({"type":"HEATING","power":"OFF","temperature":null})");
  } else {
    std::snprintf(setting, sizeof(setting), R"({"type":"HEATING","power":"ON","temperature":{"celsius":%.1f}})",
                  static_cast<double>(target));
  }
  char body[400];
  std::snprintf(body, sizeof(body),
                R"({"tadoMode":"HOME","setting":%s,)"
                R"("overlayType":%s,"overlay":%s,"nextTimeBlock":{"start":"2026-09-14T20:00:00Z"},)"
                R"("sensorDataPoints":{"insideTemperature":{"celsius":%.1f},"humidity":{"percentage":48.5}}})",
                setting, zone.overlay ? "\"MANUAL\"" : "null",
                zone.overlay ? R"({"type":"MANUAL","termination":{"typeSkillBasedApp":"NEXT_TIME_BLOCK"}})" : "null",
                static_cast<double>(zone.inside));
  return reply(out, 200, body);
}
constexpr const char* kOverlayOk =
    R"({"type":"MANUAL","setting":{"type":"HEATING","power":"ON","temperature":{"celsius":20.0}},"termination":{"type":"TIMER","typeSkillBasedApp":"NEXT_TIME_BLOCK"}})";

}  // namespace detail

// Returns the HTTP status and fills out.buf. Pairing and device-code polling
// succeed on the third attempt so the waiting screens are visible in the simulator.
inline int respond(const HttpRequest& req, HttpResponse& out) {
  using namespace detail;
  static int pairAttempts = 0;
  static int tokenPolls = 0;
  const bool isPut = std::strcmp(req.method, "PUT") == 0;
  const bool isDelete = std::strcmp(req.method, "DELETE") == 0;
  const bool isPost = std::strcmp(req.method, "POST") == 0;

  // Hue
  if (isPost && contains(req.url, "/api") && !contains(req.url, "/api/v2")) {
    return reply(out, 200, ++pairAttempts >= 3 ? kPairSuccess : kPairLinkNotPressed);
  }
  if (contains(req.url, "/clip/v2/resource/room")) return reply(out, 200, kRooms);
  if (contains(req.url, "/clip/v2/resource/grouped_light") && !contains(req.url, "/clip/v2/resource/grouped_light/")) {
    return replyGroupedLightList(out);
  }
  if (contains(req.url, "/clip/v2/resource/grouped_light/")) {
    if (contains(req.url, kAllLightsRid)) {
      if (!isPut) return reply(out, 405, R"({"errors":[{"description":"read the collection instead"}]})");
      applyLightBodyToAll(req.body);
      return reply(out, 200, kPutOk);
    }
    SimLight* light = findSimLight(req.url);
    if (light == nullptr) return reply(out, 404, R"({"errors":[{"description":"unknown grouped_light"}]})");
    if (isPut) {
      applyLightBody(*light, req.body);
      return reply(out, 200, kPutOk);
    }
    return replyLightState(out, *light);
  }

  // tado
  if (contains(req.url, "/oauth2/device_authorize")) return reply(out, 200, kDeviceAuthorize);
  if (contains(req.url, "/oauth2/token")) {
    if (contains(req.body, "grant_type=refresh_token")) return reply(out, 200, kTokenOk);
    return ++tokenPolls >= 3 ? reply(out, 200, kTokenOk) : reply(out, 400, kTokenPending);
  }
  if (contains(req.url, "/api/v2/me")) return reply(out, 200, kMe);
  if (contains(req.url, "/overlay") || contains(req.url, "/state")) {
    SimZone* zone = findSimZone(req.url);
    if (zone == nullptr) return reply(out, 404, R"({"errors":[{"code":"zoneNotFound"}]})");
    if (isDelete) {
      zone->overlay = false;
      return reply(out, 204, "");
    }
    if (isPut) {
      const char* celsius = req.body != nullptr ? std::strstr(req.body, "\"celsius\":") : nullptr;
      if (celsius != nullptr) {
        zone->overlay = true;
        zone->overlayOff = false;
        zone->overlayTarget = static_cast<float>(std::atof(celsius + 10));
      } else if (contains(req.body, "\"power\":\"OFF\"")) {
        zone->overlay = true;
        zone->overlayOff = true;
      }
      return reply(out, 200, kOverlayOk);
    }
    return replyZoneState(out, *zone);
  }
  if (contains(req.url, "/zones")) return reply(out, 200, kZones);

  return reply(out, 404, R"({"errors":[{"code":"notFound"}]})");
}

}  // namespace homecontrol::fixtures
