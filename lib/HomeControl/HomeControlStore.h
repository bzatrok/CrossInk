#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

/**
 * Persists the Home Control pairing state on the SD card:
 * the Hue bridge address and application key, and the tado° refresh token
 * plus home id. Secrets are XOR-obfuscated with the device MAC and
 * base64-encoded, the same (non-cryptographic) scheme KOReader sync uses.
 */
class HomeControlStore : public PersistableStore<HomeControlStore> {
 private:
  std::string hueBridgeIp;
  std::string hueAppKey;
  std::string tadoRefreshToken;
  int32_t tadoHomeId = 0;  // 0 = not yet resolved

  HomeControlStore() = default;
  ~HomeControlStore() = default;

  friend class PersistableStore<HomeControlStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/home-control.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Philips Hue
  bool hasHueBridge() const;
  bool hasHuePairing() const;
  const std::string& getHueBridgeIp() const {
    ensureLoaded();
    return hueBridgeIp;
  }
  const std::string& getHueAppKey() const {
    ensureLoaded();
    return hueAppKey;
  }
  void setHueBridgeIp(const std::string& ip);
  void setHueAppKey(const std::string& key);
  void clearHuePairing();  // keeps the bridge address

  // tado°
  bool hasTadoRefreshToken() const;
  const std::string& getTadoRefreshToken() const {
    ensureLoaded();
    return tadoRefreshToken;
  }
  int32_t getTadoHomeId() const {
    ensureLoaded();
    return tadoHomeId;
  }
  void setTadoRefreshToken(const std::string& token);
  void setTadoHomeId(int32_t homeId);
  void clearTado();
};

#define HOME_CONTROL_STORE HomeControlStore::getInstance()
