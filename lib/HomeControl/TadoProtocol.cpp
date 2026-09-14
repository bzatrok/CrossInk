#include "TadoProtocol.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

void copyBounded(char* out, size_t cap, const char* in) {
  if (cap == 0) return;
  if (in == nullptr) {
    out[0] = '\0';
    return;
  }
  std::strncpy(out, in, cap - 1);
  out[cap - 1] = '\0';
}

size_t finish(char* out, size_t cap, int written) {
  if (written < 0 || cap == 0) return 0;
  return static_cast<size_t>(written) < cap ? static_cast<size_t>(written) : cap - 1;
}

bool isUnreserved(const char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
         c == '.' || c == '~';
}

// Percent-encodes value into out starting at pos. Returns the new position, or cap if it did not fit.
size_t appendFormValue(char* out, size_t cap, size_t pos, const char* value) {
  for (const char* p = value; *p != '\0'; ++p) {
    if (isUnreserved(*p)) {
      if (pos + 1 >= cap) return cap;
      out[pos++] = *p;
    } else {
      if (pos + 3 >= cap) return cap;
      static const char hex[] = "0123456789ABCDEF";
      const auto byte = static_cast<unsigned char>(*p);
      out[pos++] = '%';
      out[pos++] = hex[byte >> 4];
      out[pos++] = hex[byte & 0x0F];
    }
  }
  out[pos] = '\0';
  return pos;
}

size_t appendLiteral(char* out, size_t cap, size_t pos, const char* text) {
  const size_t len = std::strlen(text);
  if (pos + len >= cap) return cap;
  std::memcpy(out + pos, text, len);
  pos += len;
  out[pos] = '\0';
  return pos;
}

size_t buildForm(char* out, size_t cap, const char* prefix, const char* valueKey, const char* value) {
  if (cap == 0) return 0;
  size_t pos = appendLiteral(out, cap, 0, prefix);
  if (pos < cap) pos = appendLiteral(out, cap, pos, valueKey);
  if (pos < cap) pos = appendFormValue(out, cap, pos, value);
  if (pos >= cap) {
    out[cap - 1] = '\0';
    return 0;  // truncated forms must never be sent
  }
  return pos;
}

}  // namespace

namespace tado {

size_t buildDeviceAuthorizeForm(char* out, size_t cap) {
  return finish(out, cap, std::snprintf(out, cap, "client_id=%s&scope=offline_access", kClientId));
}

size_t buildDeviceTokenForm(char* out, size_t cap, const char* deviceCode) {
  char prefix[128];
  std::snprintf(prefix, sizeof(prefix), "client_id=%s&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code",
                kClientId);
  return buildForm(out, cap, prefix, "&device_code=", deviceCode);
}

size_t buildRefreshForm(char* out, size_t cap, const char* refreshToken) {
  char prefix[96];
  std::snprintf(prefix, sizeof(prefix), "client_id=%s&grant_type=refresh_token", kClientId);
  return buildForm(out, cap, prefix, "&refresh_token=", refreshToken);
}

bool parseDeviceAuthorize(const char* body, size_t len, DeviceCode& out) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) return false;
  const char* deviceCode = doc["device_code"] | "";
  const char* userCode = doc["user_code"] | "";
  const char* uri = doc["verification_uri_complete"] | "";
  if (uri[0] == '\0') uri = doc["verification_uri"] | "";
  if (deviceCode[0] == '\0' || userCode[0] == '\0' || uri[0] == '\0') return false;
  copyBounded(out.deviceCode, sizeof(out.deviceCode), deviceCode);
  copyBounded(out.userCode, sizeof(out.userCode), userCode);
  copyBounded(out.verificationUri, sizeof(out.verificationUri), uri);
  const int interval = doc["interval"] | static_cast<int>(kDefaultPollIntervalSec);
  out.intervalSec = interval > 0 ? static_cast<uint16_t>(interval) : kDefaultPollIntervalSec;
  out.expiresInSec = doc["expires_in"] | 0u;
  return true;
}

TokenResult parseTokenResponse(const int httpStatus, const char* body, size_t len, Tokens& out) {
  (void)httpStatus;
  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) return TokenResult::Malformed;

  const char* accessToken = doc["access_token"] | "";
  if (accessToken[0] != '\0') {
    const char* refreshToken = doc["refresh_token"] | "";
    if (refreshToken[0] == '\0') return TokenResult::Malformed;
    // A truncated token would be rejected as unauthorized later; fail loudly here instead.
    if (std::strlen(accessToken) >= sizeof(out.accessToken) || std::strlen(refreshToken) >= sizeof(out.refreshToken)) {
      return TokenResult::Malformed;
    }
    copyBounded(out.accessToken, sizeof(out.accessToken), accessToken);
    copyBounded(out.refreshToken, sizeof(out.refreshToken), refreshToken);
    out.expiresInSec = doc["expires_in"] | 0u;
    return TokenResult::Ok;
  }

  const char* error = doc["error"] | "";
  if (std::strcmp(error, "authorization_pending") == 0) return TokenResult::AuthorizationPending;
  if (std::strcmp(error, "slow_down") == 0) return TokenResult::SlowDown;
  if (std::strcmp(error, "expired_token") == 0) return TokenResult::ExpiredToken;
  if (std::strcmp(error, "access_denied") == 0) return TokenResult::AccessDenied;
  if (std::strcmp(error, "invalid_grant") == 0) return TokenResult::InvalidGrant;
  return TokenResult::Malformed;
}

bool parseHomeId(const char* body, size_t len, int32_t& homeId) {
  JsonDocument filter;
  filter["homes"].add<JsonObject>()["id"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter.as<JsonVariantConst>())) !=
      DeserializationError::Ok) {
    return false;
  }
  JsonArrayConst homes = doc["homes"].as<JsonArrayConst>();
  if (homes.isNull() || homes.size() == 0) return false;
  const int32_t id = homes[0]["id"] | 0;
  if (id == 0) return false;
  homeId = id;
  return true;
}

void buildZonesFilter(JsonDocument& filter) {
  JsonObject entry = filter.to<JsonArray>().add<JsonObject>();
  entry["id"] = true;
  entry["name"] = true;
  entry["type"] = true;
}

bool parseZones(const char* body, size_t len, Zone* out, size_t cap, size_t& count) {
  count = 0;
  JsonDocument filter;
  buildZonesFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter.as<JsonVariantConst>())) !=
      DeserializationError::Ok) {
    return false;
  }
  JsonArrayConst zones = doc.as<JsonArrayConst>();
  if (zones.isNull()) return false;
  for (JsonObjectConst entry : zones) {
    if (count >= cap) break;
    const char* type = entry["type"] | "";
    if (std::strcmp(type, "HEATING") != 0) continue;
    Zone& zone = out[count];
    zone.id = entry["id"] | 0;
    copyBounded(zone.name, kNameLen, entry["name"] | "");
    ++count;
  }
  return true;
}

void buildZoneStateFilter(JsonDocument& filter) {
  JsonObject root = filter.to<JsonObject>();
  root["setting"]["power"] = true;
  root["setting"]["temperature"]["celsius"] = true;
  root["overlay"]["type"] = true;
  root["sensorDataPoints"]["insideTemperature"]["celsius"] = true;
  root["sensorDataPoints"]["humidity"]["percentage"] = true;
}

bool parseZoneState(const char* body, size_t len, ZoneState& out) {
  JsonDocument filter;
  buildZoneStateFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter.as<JsonVariantConst>())) !=
      DeserializationError::Ok) {
    return false;
  }
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull() || root["setting"].isNull()) return false;

  const char* power = root["setting"]["power"] | "";
  out.powerOn = std::strcmp(power, "ON") == 0;

  JsonVariantConst target = root["setting"]["temperature"]["celsius"];
  out.hasTarget = !target.isNull();
  out.targetCelsius = out.hasTarget ? target.as<float>() : 0.0f;

  JsonVariantConst inside = root["sensorDataPoints"]["insideTemperature"]["celsius"];
  out.hasInside = !inside.isNull();
  out.insideCelsius = out.hasInside ? inside.as<float>() : 0.0f;

  JsonVariantConst humidity = root["sensorDataPoints"]["humidity"]["percentage"];
  out.hasHumidity = !humidity.isNull();
  out.humidityPct = out.hasHumidity ? humidity.as<float>() : 0.0f;

  out.hasOverlay = !root["overlay"].isNull();
  return true;
}

float roundToStep(const float celsius) {
  float rounded = std::round(celsius / kStepCelsius) * kStepCelsius;
  if (rounded < kMinCelsius) rounded = kMinCelsius;
  if (rounded > kMaxCelsius) rounded = kMaxCelsius;
  return rounded;
}

float stepTarget(const float current, const float delta) { return roundToStep(current + delta); }

size_t buildOverlayBody(char* out, size_t cap, const float celsius) {
  const float target = roundToStep(celsius);
  return finish(out, cap,
                std::snprintf(out, cap,
                              R"({"setting":{"type":"HEATING","power":"ON","temperature":{"celsius":%.1f}},)"
                              R"("termination":{"typeSkillBasedApp":"NEXT_TIME_BLOCK"}})",
                              static_cast<double>(target)));
}

size_t buildHeatingOffOverlayBody(char* out, size_t cap) {
  return finish(out, cap,
                std::snprintf(out, cap,
                              R"({"setting":{"type":"HEATING","power":"OFF"},)"
                              R"("termination":{"typeSkillBasedApp":"NEXT_TIME_BLOCK"}})"));
}

size_t buildZonesPath(char* out, size_t cap, const int32_t homeId) {
  return finish(out, cap, std::snprintf(out, cap, "/homes/%ld/zones", static_cast<long>(homeId)));
}

size_t buildZoneStatePath(char* out, size_t cap, const int32_t homeId, const int32_t zoneId) {
  return finish(out, cap, std::snprintf(out, cap, "/homes/%ld/zones/%ld/state", static_cast<long>(homeId),
                                        static_cast<long>(zoneId)));
}

size_t buildZoneOverlayPath(char* out, size_t cap, const int32_t homeId, const int32_t zoneId) {
  return finish(out, cap, std::snprintf(out, cap, "/homes/%ld/zones/%ld/overlay", static_cast<long>(homeId),
                                        static_cast<long>(zoneId)));
}

}  // namespace tado
