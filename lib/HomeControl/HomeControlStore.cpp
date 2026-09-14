#include "HomeControlStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

namespace {
constexpr uint8_t CONFIG_VERSION = 1;
// Upper bounds for de-obfuscated secrets, so a corrupted file cannot make the
// decoder allocate an arbitrary amount.
constexpr size_t MAX_HUE_APP_KEY_LEN = 64;
constexpr size_t MAX_TADO_REFRESH_TOKEN_LEN = 512;

std::string readSecret(const JsonVariantConst value, const size_t maxLen, const char* label) {
  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  std::string plain = obfuscation::deobfuscateFromBase64(value | "", maxLen, &status);
  if (status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::TOO_LONG) {
    LOG_ERR("HCS", "Ignoring unreadable %s", label);
    plain.clear();
  }
  return plain;
}
}  // namespace

void HomeControlStore::toJson(JsonDocument& doc) const {
  doc["cfgVersion"] = CONFIG_VERSION;
  // Fields are serialized directly: saveToFile() already holds the store
  // mutex, and the public getters would try to lazy-load under it.
  doc["hueBridgeIp"] = hueBridgeIp;
  doc["hueAppKey_obf"] = obfuscation::obfuscateToBase64(hueAppKey);
  doc["tadoRefreshToken_obf"] = obfuscation::obfuscateToBase64(tadoRefreshToken);
  doc["tadoHomeId"] = tadoHomeId;
}

bool HomeControlStore::fromJson(JsonVariantConst doc) {
  hueBridgeIp = doc["hueBridgeIp"] | "";
  hueAppKey = readSecret(doc["hueAppKey_obf"], MAX_HUE_APP_KEY_LEN, "Hue app key");
  tadoRefreshToken = readSecret(doc["tadoRefreshToken_obf"], MAX_TADO_REFRESH_TOKEN_LEN, "tado refresh token");
  tadoHomeId = doc["tadoHomeId"] | 0;

  const uint8_t cfgVersion = doc["cfgVersion"] | static_cast<uint8_t>(0);
  if (cfgVersion < CONFIG_VERSION) {
    requestResave();  // stamp cfgVersion so future migrations run once
  }
  return true;
}

bool HomeControlStore::hasHueBridge() const {
  ensureLoaded();
  return !hueBridgeIp.empty();
}

bool HomeControlStore::hasHuePairing() const {
  ensureLoaded();
  return !hueBridgeIp.empty() && !hueAppKey.empty();
}

void HomeControlStore::setHueBridgeIp(const std::string& ip) {
  ensureLoaded();
  hueBridgeIp = ip;
}

void HomeControlStore::setHueAppKey(const std::string& key) {
  ensureLoaded();
  hueAppKey = key;
}

void HomeControlStore::clearHuePairing() {
  ensureLoaded();
  hueAppKey.clear();
}

bool HomeControlStore::hasTadoRefreshToken() const {
  ensureLoaded();
  return !tadoRefreshToken.empty();
}

void HomeControlStore::setTadoRefreshToken(const std::string& token) {
  ensureLoaded();
  tadoRefreshToken = token;
}

void HomeControlStore::setTadoHomeId(const int32_t homeId) {
  ensureLoaded();
  tadoHomeId = homeId;
}

void HomeControlStore::clearTado() {
  ensureLoaded();
  tadoRefreshToken.clear();
  tadoHomeId = 0;
}
