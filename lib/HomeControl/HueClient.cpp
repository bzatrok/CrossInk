#include "HueClient.h"

#if defined(CROSSINK_APP_CAP_HOME_CONTROL) && CROSSINK_APP_CAP_HOME_CONTROL

#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "HomeControlStore.h"

HueClient::HueClient(homecontrol::HttpSession& session, uint8_t* bodyBuf, const size_t bodyCap) : session(session) {
  response.buf = bodyBuf;
  response.cap = bodyCap;
}

const char* HueClient::errorName(const Error error) {
  switch (error) {
    case Error::Ok:
      return "ok";
    case Error::NoBridge:
      return "no bridge";
    case Error::NotPaired:
      return "not paired";
    case Error::LinkButtonNotPressed:
      return "link button not pressed";
    case Error::Unauthorized:
      return "unauthorized";
    case Error::LowMemory:
      return "low memory";
    case Error::Network:
      return "network";
    case Error::BadResponse:
      return "bad response";
    case Error::Http:
      return "http";
  }
  return "?";
}

HueClient::Error HueClient::mapStatus(const int status) const {
  if (status == homecontrol::kStatusLowMemory) return Error::LowMemory;
  if (status < 0) return Error::Network;
  if (status == 401 || status == 403) return Error::Unauthorized;
  if (status < 200 || status >= 300) return Error::Http;
  return Error::Ok;
}

HueClient::Error HueClient::call(const char* method, const char* path, const char* body, const size_t bodyLen) {
  const std::string& bridgeIp = HOME_CONTROL_STORE.getHueBridgeIp();
  if (bridgeIp.empty()) return Error::NoBridge;
  const std::string& appKey = HOME_CONTROL_STORE.getHueAppKey();
  if (appKey.empty()) return Error::NotPaired;

  std::snprintf(urlBuf, sizeof(urlBuf), "https://%s%s", bridgeIp.c_str(), path);
  homecontrol::HttpRequest req;
  req.method = method;
  req.url = urlBuf;
  req.contentType = body != nullptr ? "application/json" : nullptr;
  req.body = body;
  req.bodyLen = bodyLen;
  req.headerName = "hue-application-key";
  req.headerValue = appKey.c_str();
  lastStatus = session.request(req, response);
  const Error error = mapStatus(lastStatus);
  if (error != Error::Ok) LOG_ERR("HC", "Hue %s %s failed: %s (%d)", method, path, errorName(error), lastStatus);
  return error;
}

HueClient::Error HueClient::pair() {
  const std::string& bridgeIp = HOME_CONTROL_STORE.getHueBridgeIp();
  if (bridgeIp.empty()) return Error::NoBridge;

  std::snprintf(urlBuf, sizeof(urlBuf), "https://%s/api", bridgeIp.c_str());
  const size_t len = hue::buildPairBody(bodyBuf, sizeof(bodyBuf));
  homecontrol::HttpRequest req;
  req.method = "POST";
  req.url = urlBuf;
  req.contentType = "application/json";
  req.body = bodyBuf;
  req.bodyLen = len;
  lastStatus = session.request(req, response);
  if (lastStatus == homecontrol::kStatusLowMemory) return Error::LowMemory;
  if (lastStatus < 0) return Error::Network;
  if (lastStatus < 200 || lastStatus >= 300) {
    LOG_ERR("HC", "Hue pairing HTTP %d", lastStatus);
    return Error::Http;
  }

  char appKey[64];
  switch (hue::parsePairResponse(response.text(), response.len, appKey, sizeof(appKey))) {
    case hue::PairResult::Paired:
      HOME_CONTROL_STORE.setHueAppKey(appKey);
      LOG_INF("HC", "Hue bridge paired");
      return Error::Ok;
    case hue::PairResult::LinkButtonNotPressed:
      return Error::LinkButtonNotPressed;
    case hue::PairResult::OtherError:
      LOG_ERR("HC", "Hue pairing refused by bridge");
      return Error::Http;
    case hue::PairResult::Malformed:
      break;
  }
  LOG_ERR("HC", "Hue pairing reply not understood");
  return Error::BadResponse;
}

HueClient::Error HueClient::listRooms(hue::Room* out, const size_t cap, size_t& count) {
  const Error error = call("GET", "/clip/v2/resource/room", nullptr, 0);
  if (error != Error::Ok) return error;
  if (!hue::parseRooms(response.text(), response.len, out, cap, count)) {
    LOG_ERR("HC", "Hue room list not understood (%u bytes)", static_cast<unsigned>(response.len));
    return Error::BadResponse;
  }
  LOG_INF("HC", "Hue rooms: %u (%u bytes)", static_cast<unsigned>(count), static_cast<unsigned>(response.len));
  return Error::Ok;
}

HueClient::Error HueClient::listGroupedLights(hue::GroupedLight* out, const size_t cap, size_t& count) {
  const Error error = call("GET", "/clip/v2/resource/grouped_light", nullptr, 0);
  if (error != Error::Ok) return error;
  if (!hue::parseGroupedLights(response.text(), response.len, out, cap, count)) {
    LOG_ERR("HC", "Hue grouped light list not understood (%u bytes)", static_cast<unsigned>(response.len));
    return Error::BadResponse;
  }
  LOG_INF("HC", "Hue grouped lights: %u (%u bytes)", static_cast<unsigned>(count), static_cast<unsigned>(response.len));
  return Error::Ok;
}

HueClient::Error HueClient::getRoomState(const char* groupedLightId, hue::RoomState& out) {
  char path[96];
  hue::buildResourcePath(path, sizeof(path), "grouped_light", groupedLightId);
  const Error error = call("GET", path, nullptr, 0);
  if (error != Error::Ok) return error;
  if (!hue::parseGroupedLightState(response.text(), response.len, out)) {
    LOG_ERR("HC", "Hue grouped light state not understood");
    return Error::BadResponse;
  }
  return Error::Ok;
}

HueClient::Error HueClient::setOn(const char* groupedLightId, const bool on) {
  char path[96];
  hue::buildResourcePath(path, sizeof(path), "grouped_light", groupedLightId);
  const size_t len = hue::buildOnBody(bodyBuf, sizeof(bodyBuf), on);
  return call("PUT", path, bodyBuf, len);
}

HueClient::Error HueClient::setBrightness(const char* groupedLightId, const uint8_t percent, const bool alsoOn) {
  char path[96];
  hue::buildResourcePath(path, sizeof(path), "grouped_light", groupedLightId);
  const size_t len = hue::buildBrightnessBody(bodyBuf, sizeof(bodyBuf), percent, alsoOn);
  return call("PUT", path, bodyBuf, len);
}

#endif  // CROSSINK_APP_CAP_HOME_CONTROL
