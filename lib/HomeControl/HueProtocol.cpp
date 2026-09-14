#include "HueProtocol.h"

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

size_t writeBody(char* out, size_t cap, const char* text) {
  const int written = std::snprintf(out, cap, "%s", text);
  if (written < 0) return 0;
  return static_cast<size_t>(written) < cap ? static_cast<size_t>(written) : cap - 1;
}

}  // namespace

namespace hue {

size_t buildPairBody(char* out, size_t cap) {
  return writeBody(out, cap, R"({"devicetype":"crossink#x4pro","generateclientkey":true})");
}

PairResult parsePairResponse(const char* body, size_t len, char* appKeyOut, size_t appKeyCap) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) return PairResult::Malformed;
  JsonArrayConst entries = doc.as<JsonArrayConst>();
  if (entries.isNull() || entries.size() == 0) return PairResult::Malformed;

  JsonObjectConst first = entries[0].as<JsonObjectConst>();
  if (first.isNull()) return PairResult::Malformed;

  JsonObjectConst success = first["success"].as<JsonObjectConst>();
  if (!success.isNull()) {
    const char* username = success["username"] | "";
    if (username[0] == '\0') return PairResult::Malformed;
    copyBounded(appKeyOut, appKeyCap, username);
    return PairResult::Paired;
  }

  JsonObjectConst error = first["error"].as<JsonObjectConst>();
  if (error.isNull()) return PairResult::Malformed;
  const int type = error["type"] | 0;
  return type == 101 ? PairResult::LinkButtonNotPressed : PairResult::OtherError;
}

void buildRoomsFilter(JsonDocument& filter) {
  JsonObject root = filter.to<JsonObject>();
  JsonObject entry = root["data"].add<JsonObject>();
  entry["id"] = true;
  entry["metadata"]["name"] = true;
  JsonObject service = entry["services"].add<JsonObject>();
  service["rid"] = true;
  service["rtype"] = true;
}

bool parseRooms(const char* body, size_t len, Room* out, size_t cap, size_t& count) {
  count = 0;
  JsonDocument filter;
  buildRoomsFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter.as<JsonVariantConst>())) !=
      DeserializationError::Ok) {
    return false;
  }
  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) return false;

  for (JsonObjectConst entry : data) {
    if (count >= cap) break;
    Room& room = out[count];
    copyBounded(room.id, kIdLen, entry["id"] | "");
    copyBounded(room.name, kNameLen, entry["metadata"]["name"] | "");
    room.groupedLightId[0] = '\0';
    for (JsonObjectConst service : entry["services"].as<JsonArrayConst>()) {
      const char* rtype = service["rtype"] | "";
      if (std::strcmp(rtype, "grouped_light") == 0) {
        copyBounded(room.groupedLightId, kIdLen, service["rid"] | "");
        break;
      }
    }
    if (room.id[0] == '\0') continue;  // skip entries without an id rather than counting them
    ++count;
  }
  return true;
}

void buildGroupedLightFilter(JsonDocument& filter) {
  JsonObject root = filter.to<JsonObject>();
  JsonObject entry = root["data"].add<JsonObject>();
  entry["on"]["on"] = true;
  entry["dimming"]["brightness"] = true;
}

bool parseGroupedLightState(const char* body, size_t len, RoomState& out) {
  JsonDocument filter;
  buildGroupedLightFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter.as<JsonVariantConst>())) !=
      DeserializationError::Ok) {
    return false;
  }
  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull() || data.size() == 0) return false;
  JsonObjectConst first = data[0].as<JsonObjectConst>();
  out.on = first["on"]["on"] | false;
  const float brightness = first["dimming"]["brightness"] | 0.0f;
  out.brightness = static_cast<uint8_t>(brightness < 0.0f ? 0 : (brightness > 100.0f ? 100 : brightness));
  return true;
}

void buildGroupedLightListFilter(JsonDocument& filter) {
  JsonObject root = filter.to<JsonObject>();
  JsonObject entry = root["data"].add<JsonObject>();
  entry["id"] = true;
  entry["owner"]["rtype"] = true;
  entry["on"]["on"] = true;
  entry["dimming"]["brightness"] = true;
}

bool parseGroupedLights(const char* body, size_t len, GroupedLight* out, size_t cap, size_t& count) {
  count = 0;
  JsonDocument filter;
  buildGroupedLightListFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter.as<JsonVariantConst>())) !=
      DeserializationError::Ok) {
    return false;
  }
  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) return false;

  for (JsonObjectConst entry : data) {
    if (count >= cap) break;
    const char* id = entry["id"] | "";
    if (id[0] == '\0') continue;
    GroupedLight& group = out[count];
    copyBounded(group.id, kIdLen, id);
    const char* ownerType = entry["owner"]["rtype"] | "";
    group.ownedByBridgeHome = std::strcmp(ownerType, "bridge_home") == 0;
    group.state.on = entry["on"]["on"] | false;
    const float brightness = entry["dimming"]["brightness"] | 0.0f;
    group.state.brightness = static_cast<uint8_t>(brightness < 0.0f ? 0 : (brightness > 100.0f ? 100 : brightness));
    ++count;
  }
  return true;
}

const GroupedLight* findGroupedLight(const GroupedLight* list, const size_t count, const char* id) {
  if (list == nullptr || id == nullptr || id[0] == '\0') return nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(list[i].id, id) == 0) return &list[i];
  }
  return nullptr;
}

size_t buildOnBody(char* out, size_t cap, const bool on) {
  return writeBody(out, cap, on ? R"({"on":{"on":true}})" : R"({"on":{"on":false}})");
}

size_t buildBrightnessBody(char* out, size_t cap, const uint8_t percent, const bool alsoOn) {
  const uint8_t clamped = clampBrightness(percent);
  const int written = alsoOn ? std::snprintf(out, cap, R"({"on":{"on":true},"dimming":{"brightness":%u}})", clamped)
                             : std::snprintf(out, cap, R"({"dimming":{"brightness":%u}})", clamped);
  if (written < 0) return 0;
  return static_cast<size_t>(written) < cap ? static_cast<size_t>(written) : cap - 1;
}

uint8_t clampBrightness(const int value) {
  if (value < kMinBrightness) return kMinBrightness;
  if (value > kMaxBrightness) return kMaxBrightness;
  return static_cast<uint8_t>(value);
}

size_t buildResourcePath(char* out, size_t cap, const char* type, const char* rid) {
  const int written = std::snprintf(out, cap, "/clip/v2/resource/%s/%s", type, rid);
  if (written < 0) return 0;
  return static_cast<size_t>(written) < cap ? static_cast<size_t>(written) : cap - 1;
}

}  // namespace hue
