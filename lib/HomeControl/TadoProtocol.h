#pragma once

// Pure tado° v2 protocol helpers: OAuth device-code forms, REST request
// bodies, and response parsers over plain buffers. No Arduino, Wi-Fi, or HTTP
// includes so the file is unit-testable on the host and safe on every target.

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

namespace tado {

// Public client id published by tado for third-party device-code clients.
constexpr const char* kClientId = "1bb50063-6b0c-4d11-bd99-387f4a91cc46";
constexpr const char* kAuthBaseUrl = "https://login.tado.com/oauth2";
constexpr const char* kApiBaseUrl = "https://my.tado.com/api/v2";

constexpr float kMinCelsius = 5.0f;
constexpr float kMaxCelsius = 25.0f;
constexpr float kStepCelsius = 0.5f;
constexpr uint16_t kDefaultPollIntervalSec = 5;
constexpr size_t kMaxZones = 12;
constexpr size_t kNameLen = 33;

struct DeviceCode {
  char deviceCode[160];
  char userCode[16];
  char verificationUri[160];  // verification_uri_complete when present, else verification_uri
  uint16_t intervalSec = kDefaultPollIntervalSec;
  uint32_t expiresInSec = 0;
};

enum class TokenResult : uint8_t {
  Ok,
  AuthorizationPending,
  SlowDown,
  ExpiredToken,
  AccessDenied,
  InvalidGrant,
  Malformed,
};

struct Tokens {
  // Callers own the storage; sizes cover tado's JWT access tokens and opaque refresh tokens.
  // tado access tokens are JWTs well over 1 KB; refresh tokens are short opaque strings.
  char accessToken[3072];
  char refreshToken[512];
  uint32_t expiresInSec = 0;
};

struct Zone {
  int32_t id = 0;
  char name[kNameLen];
};

struct ZoneState {
  bool hasInside = false;
  float insideCelsius = 0.0f;
  bool hasHumidity = false;
  float humidityPct = 0.0f;
  bool powerOn = false;
  bool hasTarget = false;
  float targetCelsius = 0.0f;
  bool hasOverlay = false;  // false means the zone follows its schedule
};

// application/x-www-form-urlencoded bodies. All return bytes written (excluding NUL).
size_t buildDeviceAuthorizeForm(char* out, size_t cap);
size_t buildDeviceTokenForm(char* out, size_t cap, const char* deviceCode);
size_t buildRefreshForm(char* out, size_t cap, const char* refreshToken);

bool parseDeviceAuthorize(const char* body, size_t len, DeviceCode& out);
// httpStatus is informational; the JSON body decides the result.
TokenResult parseTokenResponse(int httpStatus, const char* body, size_t len, Tokens& out);

// GET /me -> homes[0].id
bool parseHomeId(const char* body, size_t len, int32_t& homeId);

// GET /homes/{h}/zones -> HEATING zones only.
void buildZonesFilter(JsonDocument& filter);
bool parseZones(const char* body, size_t len, Zone* out, size_t cap, size_t& count);

// GET /homes/{h}/zones/{z}/state
void buildZoneStateFilter(JsonDocument& filter);
bool parseZoneState(const char* body, size_t len, ZoneState& out);

// Nearest 0.5 °C, then clamped to the tado range.
float roundToStep(float celsius);
float stepTarget(float current, float delta);

// PUT /homes/{h}/zones/{z}/overlay body: heating on at the rounded target until the next schedule block.
size_t buildOverlayBody(char* out, size_t cap, float celsius);
// Same endpoint: heating off until the next schedule block (no target temperature).
size_t buildHeatingOffOverlayBody(char* out, size_t cap);

// REST paths relative to kApiBaseUrl. Return bytes written.
size_t buildZonesPath(char* out, size_t cap, int32_t homeId);
size_t buildZoneStatePath(char* out, size_t cap, int32_t homeId, int32_t zoneId);
size_t buildZoneOverlayPath(char* out, size_t cap, int32_t homeId, int32_t zoneId);

}  // namespace tado
