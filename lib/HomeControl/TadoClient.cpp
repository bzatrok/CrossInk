#include "TadoClient.h"

#if defined(CROSSINK_APP_CAP_HOME_CONTROL) && CROSSINK_APP_CAP_HOME_CONTROL

#include <Arduino.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "HomeControlStore.h"

namespace {
// Refresh a little before tado's stated expiry so a request never races the deadline.
constexpr uint32_t ACCESS_TOKEN_MARGIN_SEC = 60;
constexpr uint32_t ACCESS_TOKEN_FALLBACK_SEC = 540;
}  // namespace

TadoClient::TadoClient(homecontrol::HttpSession& session, uint8_t* bodyBuf, const size_t bodyCap) : session(session) {
  response.buf = bodyBuf;
  response.cap = bodyCap;
  tokens.accessToken[0] = '\0';
  tokens.refreshToken[0] = '\0';
  authHeader[0] = '\0';
}

const char* TadoClient::errorName(const Error error) {
  switch (error) {
    case Error::Ok:
      return "ok";
    case Error::NotPaired:
      return "not paired";
    case Error::Pending:
      return "pending";
    case Error::SlowDown:
      return "slow down";
    case Error::Expired:
      return "expired";
    case Error::Denied:
      return "denied";
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

TadoClient::Error TadoClient::mapStatus(const int status) const {
  if (status == homecontrol::kStatusLowMemory) return Error::LowMemory;
  if (status < 0) return Error::Network;
  if (status < 200 || status >= 300) return Error::Http;
  return Error::Ok;
}

bool TadoClient::accessTokenValid() const {
  if (tokens.accessToken[0] == '\0') return false;
  // Signed subtraction so a wrapped millis() still compares correctly.
  return static_cast<int32_t>(accessExpiresAtMs - millis()) > 0;
}

TadoClient::Error TadoClient::startDeviceAuth(tado::DeviceCode& out) {
  const size_t formLen = tado::buildDeviceAuthorizeForm(formBuf, sizeof(formBuf));
  std::snprintf(urlBuf, sizeof(urlBuf), "%s/device_authorize", tado::kAuthBaseUrl);
  homecontrol::HttpRequest req;
  req.method = "POST";
  req.url = urlBuf;
  req.contentType = "application/x-www-form-urlencoded";
  req.body = formBuf;
  req.bodyLen = formLen;
  lastStatus = session.request(req, response);
  const Error error = mapStatus(lastStatus);
  if (error != Error::Ok) {
    LOG_ERR("HC", "tado device_authorize failed: %s (%d)", errorName(error), lastStatus);
    return error;
  }
  if (!tado::parseDeviceAuthorize(response.text(), response.len, out)) {
    LOG_ERR("HC", "tado device_authorize reply not understood");
    return Error::BadResponse;
  }
  LOG_INF("HC", "tado device code issued, poll every %u s for %u s", out.intervalSec, out.expiresInSec);
  return Error::Ok;
}

TadoClient::Error TadoClient::tokenRequest(const char* form, const size_t formLen) {
  std::snprintf(urlBuf, sizeof(urlBuf), "%s/token", tado::kAuthBaseUrl);
  homecontrol::HttpRequest req;
  req.method = "POST";
  req.url = urlBuf;
  req.contentType = "application/x-www-form-urlencoded";
  req.body = form;
  req.bodyLen = formLen;
  lastStatus = session.request(req, response);
  if (lastStatus == homecontrol::kStatusLowMemory) return Error::LowMemory;
  if (lastStatus < 0) return Error::Network;

  switch (tado::parseTokenResponse(lastStatus, response.text(), response.len, tokens)) {
    case tado::TokenResult::Ok: {
      const uint32_t lifetime = tokens.expiresInSec > ACCESS_TOKEN_MARGIN_SEC
                                    ? tokens.expiresInSec - ACCESS_TOKEN_MARGIN_SEC
                                    : ACCESS_TOKEN_FALLBACK_SEC;
      accessExpiresAtMs = millis() + lifetime * 1000UL;
      std::snprintf(authHeader, sizeof(authHeader), "Bearer %s", tokens.accessToken);
      return persistTokens();
    }
    case tado::TokenResult::AuthorizationPending:
      return Error::Pending;
    case tado::TokenResult::SlowDown:
      return Error::SlowDown;
    case tado::TokenResult::ExpiredToken:
      return Error::Expired;
    case tado::TokenResult::AccessDenied:
      return Error::Denied;
    case tado::TokenResult::InvalidGrant:
      LOG_ERR("HC", "tado refresh token rejected; pairing required again");
      return Error::NotPaired;
    case tado::TokenResult::Malformed:
      break;
  }
  LOG_ERR("HC", "tado token reply not understood (HTTP %d)", lastStatus);
  return Error::BadResponse;
}

TadoClient::Error TadoClient::persistTokens() {
  // tado rotates the refresh token on every use; the old one is dead as soon
  // as this reply arrived, so the new one must reach the SD card immediately.
  HOME_CONTROL_STORE.setTadoRefreshToken(tokens.refreshToken);
  if (!HOME_CONTROL_STORE.saveToFile()) {
    LOG_ERR("HC", "Could not save tado refresh token; re-pairing may be needed after a reboot");
  }
  LOG_INF("HC", "tado tokens updated (access token %u chars, refresh token %u chars, expires in %u s)",
          static_cast<unsigned>(std::strlen(tokens.accessToken)), static_cast<unsigned>(std::strlen(tokens.refreshToken)),
          static_cast<unsigned>(tokens.expiresInSec));
  return Error::Ok;
}

TadoClient::Error TadoClient::pollDeviceToken(const char* deviceCode) {
  const size_t formLen = tado::buildDeviceTokenForm(formBuf, sizeof(formBuf), deviceCode);
  if (formLen == 0) {
    LOG_ERR("HC", "tado device code too long for the token form");
    return Error::BadResponse;
  }
  return tokenRequest(formBuf, formLen);
}

TadoClient::Error TadoClient::ensureAccessToken() {
  if (accessTokenValid()) return Error::Ok;
  const std::string& refreshToken = HOME_CONTROL_STORE.getTadoRefreshToken();
  if (refreshToken.empty()) return Error::NotPaired;
  const size_t formLen = tado::buildRefreshForm(formBuf, sizeof(formBuf), refreshToken.c_str());
  if (formLen == 0) {
    LOG_ERR("HC", "tado refresh token too long for the token form");
    return Error::NotPaired;
  }
  return tokenRequest(formBuf, formLen);
}

TadoClient::Error TadoClient::authed(const char* method, const char* path, const char* body, const size_t bodyLen) {
  Error error = ensureAccessToken();
  if (error != Error::Ok) return error;

  for (int attempt = 0; attempt < 2; ++attempt) {
    std::snprintf(urlBuf, sizeof(urlBuf), "%s%s", tado::kApiBaseUrl, path);
    homecontrol::HttpRequest req;
    req.method = method;
    req.url = urlBuf;
    req.contentType = body != nullptr ? "application/json" : nullptr;
    req.body = body;
    req.bodyLen = bodyLen;
    req.headerName = "Authorization";
    req.headerValue = authHeader;
    lastStatus = session.request(req, response);
    if (lastStatus != 401 || attempt == 1) break;

    // Token was revoked server-side before its stated expiry: refresh once.
    LOG_ERR("HC", "tado 401 on %s: %.160s", path, response.text());
    tokens.accessToken[0] = '\0';
    error = ensureAccessToken();
    if (error != Error::Ok) return error;
  }

  if (lastStatus == 401) {
    LOG_ERR("HC", "tado still unauthorized after refresh: %.160s", response.text());
    return Error::NotPaired;
  }
  error = mapStatus(lastStatus);
  if (error != Error::Ok) LOG_ERR("HC", "tado %s %s failed: %s (%d)", method, path, errorName(error), lastStatus);
  return error;
}

TadoClient::Error TadoClient::fetchHomeId(int32_t& homeId) {
  const int32_t cached = HOME_CONTROL_STORE.getTadoHomeId();
  if (cached != 0) {
    homeId = cached;
    return Error::Ok;
  }
  const Error error = authed("GET", "/me", nullptr, 0);
  if (error != Error::Ok) return error;
  if (!tado::parseHomeId(response.text(), response.len, homeId)) {
    LOG_ERR("HC", "tado /me has no home");
    return Error::BadResponse;
  }
  HOME_CONTROL_STORE.setTadoHomeId(homeId);
  if (!HOME_CONTROL_STORE.saveToFile()) LOG_ERR("HC", "Could not cache tado home id");
  return Error::Ok;
}

TadoClient::Error TadoClient::listZones(const int32_t homeId, tado::Zone* out, const size_t cap, size_t& count) {
  char path[64];
  tado::buildZonesPath(path, sizeof(path), homeId);
  const Error error = authed("GET", path, nullptr, 0);
  if (error != Error::Ok) return error;
  if (!tado::parseZones(response.text(), response.len, out, cap, count)) {
    LOG_ERR("HC", "tado zone list not understood");
    return Error::BadResponse;
  }
  LOG_INF("HC", "tado heating zones: %u", static_cast<unsigned>(count));
  return Error::Ok;
}

TadoClient::Error TadoClient::getZoneState(const int32_t homeId, const int32_t zoneId, tado::ZoneState& out) {
  char path[64];
  tado::buildZoneStatePath(path, sizeof(path), homeId, zoneId);
  const Error error = authed("GET", path, nullptr, 0);
  if (error != Error::Ok) return error;
  if (!tado::parseZoneState(response.text(), response.len, out)) {
    LOG_ERR("HC", "tado zone state not understood");
    return Error::BadResponse;
  }
  return Error::Ok;
}

TadoClient::Error TadoClient::setOverlay(const int32_t homeId, const int32_t zoneId, const float celsius) {
  char path[64];
  tado::buildZoneOverlayPath(path, sizeof(path), homeId, zoneId);
  const size_t len = tado::buildOverlayBody(bodyBuf, sizeof(bodyBuf), celsius);
  return authed("PUT", path, bodyBuf, len);
}

TadoClient::Error TadoClient::setHeatingOff(const int32_t homeId, const int32_t zoneId) {
  char path[64];
  tado::buildZoneOverlayPath(path, sizeof(path), homeId, zoneId);
  const size_t len = tado::buildHeatingOffOverlayBody(bodyBuf, sizeof(bodyBuf));
  return authed("PUT", path, bodyBuf, len);
}

TadoClient::Error TadoClient::clearOverlay(const int32_t homeId, const int32_t zoneId) {
  char path[64];
  tado::buildZoneOverlayPath(path, sizeof(path), homeId, zoneId);
  return authed("DELETE", path, nullptr, 0);
}

#endif  // CROSSINK_APP_CAP_HOME_CONTROL
