#pragma once

// Pure Philips Hue CLIP v2 protocol helpers: request bodies and response
// parsers over plain buffers. No Arduino, Wi-Fi, or HTTP includes so the
// file is unit-testable on the host and safe to compile on every target.

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

namespace hue {

constexpr size_t kIdLen = 37;    // UUID text plus NUL
constexpr size_t kNameLen = 33;  // room name, truncated
constexpr size_t kMaxRooms = 16;
// Rooms and zones each own one grouped_light, plus the bridge_home "all lights" group.
constexpr size_t kMaxGroupedLights = 32;
constexpr uint8_t kMinBrightness = 1;  // Hue treats 0 as "lowest possible", so 1 is the floor
constexpr uint8_t kMaxBrightness = 100;
constexpr uint8_t kBrightnessStep = 10;

struct Room {
  char id[kIdLen];
  char name[kNameLen];
  char groupedLightId[kIdLen];  // empty when the room exposes no grouped_light service
};

struct RoomState {
  bool on = false;
  uint8_t brightness = 0;  // percent 0..100
};

// One entry of GET /clip/v2/resource/grouped_light. The group owned by the
// bridge_home resource covers every light on the bridge.
struct GroupedLight {
  char id[kIdLen];
  bool ownedByBridgeHome;
  RoomState state;
};

// The scene a room should come back to when it is switched on: the scene
// with the latest status.last_recall, an active scene winning over an
// inactive one. sceneId is empty when the room has no scene.
constexpr size_t kTimestampLen = 32;
struct RoomScene {
  char sceneId[kIdLen];
  char name[kNameLen];
  bool active;
  char lastRecall[kTimestampLen];  // ISO 8601 UTC, compares lexicographically
};

enum class PairResult : uint8_t { Paired, LinkButtonNotPressed, Malformed, OtherError };

// POST /api body. Returns bytes written (excluding NUL).
size_t buildPairBody(char* out, size_t cap);

// Parses the POST /api reply. On Paired the application key is copied to appKeyOut.
PairResult parsePairResponse(const char* body, size_t len, char* appKeyOut, size_t appKeyCap);

// Filter for GET /clip/v2/resource/room so the JSON document stays small.
void buildRoomsFilter(JsonDocument& filter);
// Parses the room list. Rooms beyond cap are dropped; returns false only on malformed JSON.
bool parseRooms(const char* body, size_t len, Room* out, size_t cap, size_t& count);

// Filter and parser for GET /clip/v2/resource/grouped_light/<rid>.
void buildGroupedLightFilter(JsonDocument& filter);
bool parseGroupedLightState(const char* body, size_t len, RoomState& out);

// Filter and parser for the whole grouped_light collection, one round trip for
// every room's state. Entries beyond cap are dropped; false only on malformed JSON.
void buildGroupedLightListFilter(JsonDocument& filter);
bool parseGroupedLights(const char* body, size_t len, GroupedLight* out, size_t cap, size_t& count);
// Entry with the given id, or nullptr.
const GroupedLight* findGroupedLight(const GroupedLight* list, size_t count, const char* id);

// Filter and parser for GET /clip/v2/resource/scene. Writes one RoomScene per
// entry of rooms (same order). Returns false only on malformed JSON.
void buildScenesFilter(JsonDocument& filter);
bool parseLastScenes(const char* body, size_t len, const Room* rooms, size_t roomCount, RoomScene* out);
// PUT /clip/v2/resource/scene/<id> body that replays the scene. Returns bytes written.
size_t buildSceneRecallBody(char* out, size_t cap);

// PUT bodies for grouped_light. Return bytes written.
size_t buildOnBody(char* out, size_t cap, bool on);
// When alsoOn is set the body also switches the group on, which Hue needs when
// brightening a group that is currently off.
size_t buildBrightnessBody(char* out, size_t cap, uint8_t percent, bool alsoOn);

uint8_t clampBrightness(int value);

// "/clip/v2/resource/<type>/<rid>". Returns bytes written.
size_t buildResourcePath(char* out, size_t cap, const char* type, const char* rid);

}  // namespace hue
