#include "TrmnlProtocol.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace {

// Copies in to out. False (and an empty out) when it would be cut short: a
// truncated URL or key is worse than a clear failure.
bool copyExact(char* out, size_t cap, const char* in) {
  if (cap == 0) return false;
  out[0] = '\0';
  if (in == nullptr) return true;
  const size_t len = std::strlen(in);
  if (len >= cap) return false;
  std::memcpy(out, in, len + 1);
  return true;
}

}  // namespace

namespace trmnl {

bool parseSetup(const char* json, size_t len, SetupResult& out) {
  out = SetupResult{};
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  const char* apiKey = root["api_key"] | "";
  if (apiKey[0] == '\0' || !copyExact(out.apiKey, sizeof(out.apiKey), apiKey)) return false;
  if (!copyExact(out.friendlyId, sizeof(out.friendlyId), root["friendly_id"] | "")) return false;
  out.ok = true;
  return true;
}

bool parseDisplay(const char* json, size_t len, DisplayResult& out) {
  out = DisplayResult{};
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  out.status = root["status"] | 0;
  if (!copyExact(out.imageUrl, sizeof(out.imageUrl), root["image_url"] | "")) return false;
  if (!copyExact(out.filename, sizeof(out.filename), root["filename"] | "")) return false;
  out.ok = true;
  return true;
}

bool isNoChange(const DisplayResult& r, const char* lastFilename) {
  if (r.status == 202) return true;
  return r.filename[0] != '\0' && lastFilename != nullptr && std::strcmp(r.filename, lastFilename) == 0;
}

bool resolveImageUrl(const char* base, const char* imageUrl, char* out, size_t cap) {
  if (cap == 0) return false;
  out[0] = '\0';
  if (base == nullptr || imageUrl == nullptr || imageUrl[0] == '\0') return false;

  if (std::strstr(imageUrl, "://") != nullptr) return copyExact(out, cap, imageUrl);

  const char* schemeEnd = std::strstr(base, "://");
  if (schemeEnd == nullptr || schemeEnd == base) return false;
  const char* hostStart = schemeEnd + 3;
  const char* originEnd = std::strchr(hostStart, '/');
  const size_t originLen = originEnd != nullptr ? static_cast<size_t>(originEnd - base) : std::strlen(base);
  if (originLen == static_cast<size_t>(hostStart - base)) return false;  // no host

  const char* sep = imageUrl[0] == '/' ? "" : "/";
  const int written = std::snprintf(out, cap, "%.*s%s%s", static_cast<int>(originLen), base, sep, imageUrl);
  if (written < 0 || static_cast<size_t>(written) >= cap) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool firmwareVersionCore(const char* version, char* out, size_t cap) {
  if (cap == 0) return false;
  out[0] = '\0';
  if (version == nullptr) return false;
  size_t i = 0;
  for (int part = 0; part < 3; ++part) {
    if (part > 0) {
      if (version[i] != '.') return false;
      ++i;
    }
    const size_t digitsStart = i;
    while (version[i] >= '0' && version[i] <= '9') ++i;
    if (i == digitsStart) return false;
  }
  if (i >= cap) return false;
  std::memcpy(out, version, i);
  out[i] = '\0';
  return true;
}

}  // namespace trmnl
