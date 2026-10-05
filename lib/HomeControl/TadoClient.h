#pragma once

#include <cstddef>
#include <cstdint>

#include "HomeControlHttp.h"
#include "TadoProtocol.h"

// tado° cloud client. Handles the OAuth device-code pairing, keeps the short
// lived access token in RAM, and persists every rotated refresh token through
// HOME_CONTROL_STORE. Owns no heap: the caller passes the response buffer.
class TadoClient {
 public:
  enum class Error : uint8_t {
    Ok,
    NotPaired,  // no refresh token, or it was rejected; caller starts device auth
    Pending,    // device code not yet approved, poll again
    SlowDown,   // poll again, later
    Expired,    // device code expired, request a new one
    Denied,     // user declined
    LowMemory,
    Network,
    BadResponse,
    Http,  // other non-2xx, see lastHttpStatus()
  };

  TadoClient(homecontrol::HttpSession& session, uint8_t* bodyBuf, size_t bodyCap);

  Error startDeviceAuth(tado::DeviceCode& out);
  // One poll of the token endpoint. On Ok the refresh token is stored and saved.
  Error pollDeviceToken(const char* deviceCode);
  // Makes sure a usable access token is in RAM, refreshing from the stored refresh token if needed.
  Error ensureAccessToken();

  Error fetchHomeId(int32_t& homeId);  // also cached in the store
  Error listZones(int32_t homeId, tado::Zone* out, size_t cap, size_t& count);
  Error getZoneState(int32_t homeId, int32_t zoneId, tado::ZoneState& out);
  Error setOverlay(int32_t homeId, int32_t zoneId, float celsius);
  Error setHeatingOff(int32_t homeId, int32_t zoneId);
  Error clearOverlay(int32_t homeId, int32_t zoneId);

  int lastHttpStatus() const { return lastStatus; }
  static const char* errorName(Error error);  // for logs only

 private:
  Error tokenRequest(const char* form, size_t formLen);
  Error persistTokens();
  // Authenticated call against kApiBaseUrl. On 401 refreshes once and retries once.
  Error authed(const char* method, const char* path, const char* body, size_t bodyLen);
  Error mapStatus(int status) const;
  bool accessTokenValid() const;

  homecontrol::HttpSession& session;
  homecontrol::HttpResponse response;
  int lastStatus = 0;
  tado::Tokens tokens;  // accessToken in RAM only; refreshToken mirrors the store
  uint32_t accessExpiresAtMs = 0;
  char urlBuf[192];
  char bodyBuf[200];
  char formBuf[1700];     // refresh token (≤512 chars) percent-encoded plus fixed fields
  char authHeader[3088];  // "Bearer " + access token
};
