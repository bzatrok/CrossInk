#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "HueProtocol.h"
#include "TadoProtocol.h"

namespace {

// ---------------------------------------------------------------- Hue

TEST(HuePairing, BodyMatchesDocumentedShape) {
  char body[96];
  const size_t len = hue::buildPairBody(body, sizeof(body));
  EXPECT_EQ(std::string(body, len), R"({"devicetype":"crossink#x4pro","generateclientkey":true})");
}

TEST(HuePairing, LinkButtonNotPressedLeavesKeyUntouched) {
  const char* reply = R"([{"error":{"type":101,"address":"","description":"link button not pressed"}}])";
  char key[64] = "unchanged";
  EXPECT_EQ(hue::parsePairResponse(reply, std::strlen(reply), key, sizeof(key)), hue::PairResult::LinkButtonNotPressed);
  EXPECT_STREQ(key, "unchanged");
}

TEST(HuePairing, SuccessCopiesApplicationKey) {
  const char* reply = R"([{"success":{"username":"a4e08834-0893-4013-b646-738582ec15c9","clientkey":"8c7f"}}])";
  char key[64] = "";
  EXPECT_EQ(hue::parsePairResponse(reply, std::strlen(reply), key, sizeof(key)), hue::PairResult::Paired);
  EXPECT_STREQ(key, "a4e08834-0893-4013-b646-738582ec15c9");
}

TEST(HuePairing, OtherErrorTypeIsNotLinkButton) {
  const char* reply = R"([{"error":{"type":7,"description":"invalid value"}}])";
  char key[64] = "";
  EXPECT_EQ(hue::parsePairResponse(reply, std::strlen(reply), key, sizeof(key)), hue::PairResult::OtherError);
}

TEST(HuePairing, MalformedRepliesAreRejected) {
  char key[64] = "";
  const char* notArray = R"({"success":{"username":"x"}})";
  EXPECT_EQ(hue::parsePairResponse(notArray, std::strlen(notArray), key, sizeof(key)), hue::PairResult::Malformed);
  const char* truncated = R"([{"success":{"username":)";
  EXPECT_EQ(hue::parsePairResponse(truncated, std::strlen(truncated), key, sizeof(key)), hue::PairResult::Malformed);
  const char* empty = "[]";
  EXPECT_EQ(hue::parsePairResponse(empty, std::strlen(empty), key, sizeof(key)), hue::PairResult::Malformed);
}

const char* kRoomsReply = R"({"errors":[],"data":[
  {"id":"11111111-1111-1111-1111-111111111111","id_v1":"/groups/1","type":"room",
   "children":[{"rid":"dev-1","rtype":"device"},{"rid":"dev-2","rtype":"device"}],
   "services":[{"rid":"gl-1","rtype":"grouped_light"},{"rid":"x","rtype":"grouped_light_level"}],
   "metadata":{"name":"Living room","archetype":"living_room"}},
  {"id":"22222222-2222-2222-2222-222222222222","type":"room","children":[],
   "services":[],"metadata":{"name":"Empty room","archetype":"other"}},
  {"id":"33333333-3333-3333-3333-333333333333","type":"room","children":[],
   "services":[{"rid":"gl-3","rtype":"grouped_light"}],
   "metadata":{"name":"A very long room name that exceeds thirty-two characters","archetype":"bedroom"}}
]})";

TEST(HueRooms, ParsesIdsNamesAndGroupedLightService) {
  hue::Room rooms[hue::kMaxRooms];
  size_t count = 99;
  ASSERT_TRUE(hue::parseRooms(kRoomsReply, std::strlen(kRoomsReply), rooms, hue::kMaxRooms, count));
  ASSERT_EQ(count, 3u);
  EXPECT_STREQ(rooms[0].id, "11111111-1111-1111-1111-111111111111");
  EXPECT_STREQ(rooms[0].name, "Living room");
  EXPECT_STREQ(rooms[0].groupedLightId, "gl-1");
  EXPECT_STREQ(rooms[1].groupedLightId, "");  // no grouped_light service
  EXPECT_STREQ(rooms[2].groupedLightId, "gl-3");
  EXPECT_EQ(std::strlen(rooms[2].name), hue::kNameLen - 1);  // truncated, NUL-terminated
}

TEST(HueRooms, CapacityIsRespected) {
  std::string big = R"({"data":[)";
  for (int i = 0; i < 20; ++i) {
    if (i > 0) big += ",";
    big += R"({"id":"room-)" + std::to_string(i) + R"(","metadata":{"name":"R"},"services":[]})";
  }
  big += "]}";
  hue::Room rooms[hue::kMaxRooms];
  size_t count = 0;
  ASSERT_TRUE(hue::parseRooms(big.c_str(), big.size(), rooms, hue::kMaxRooms, count));
  EXPECT_EQ(count, hue::kMaxRooms);
}

TEST(HueRooms, MalformedBodyFails) {
  hue::Room rooms[2];
  size_t count = 0;
  const char* bad = R"({"data":)";
  EXPECT_FALSE(hue::parseRooms(bad, std::strlen(bad), rooms, 2, count));
  const char* noData = R"({"errors":[]})";
  EXPECT_FALSE(hue::parseRooms(noData, std::strlen(noData), rooms, 2, count));
}

TEST(HueGroupedLight, ParsesOnAndBrightness) {
  const char* reply = R"({"errors":[],"data":[{"id":"gl-1","on":{"on":true},"dimming":{"brightness":57.4},"color":{}}]})";
  hue::RoomState state;
  ASSERT_TRUE(hue::parseGroupedLightState(reply, std::strlen(reply), state));
  EXPECT_TRUE(state.on);
  EXPECT_EQ(state.brightness, 57);
}

TEST(HueGroupedLight, MissingDimmingIsZero) {
  const char* reply = R"({"data":[{"id":"gl-1","on":{"on":false}}]})";
  hue::RoomState state;
  ASSERT_TRUE(hue::parseGroupedLightState(reply, std::strlen(reply), state));
  EXPECT_FALSE(state.on);
  EXPECT_EQ(state.brightness, 0);
}

TEST(HueGroupedLight, EmptyDataFails) {
  const char* reply = R"({"data":[]})";
  hue::RoomState state;
  EXPECT_FALSE(hue::parseGroupedLightState(reply, std::strlen(reply), state));
}

TEST(HueGroupedLightList, ParsesStatesAndFlagsBridgeHome) {
  const char* reply = R"({"errors":[],"data":[
    {"id":"gl-1","type":"grouped_light","owner":{"rid":"r1","rtype":"room"},"on":{"on":true},"dimming":{"brightness":40.2}},
    {"id":"gl-all","type":"grouped_light","owner":{"rid":"bh","rtype":"bridge_home"},"on":{"on":true},"dimming":{"brightness":70}},
    {"id":"","type":"grouped_light"},
    {"id":"gl-2","type":"grouped_light","owner":{"rid":"z1","rtype":"zone"},"on":{"on":false}}]})";
  hue::GroupedLight groups[4];
  size_t count = 99;
  ASSERT_TRUE(hue::parseGroupedLights(reply, std::strlen(reply), groups, 4, count));
  ASSERT_EQ(count, 3u);
  EXPECT_STREQ(groups[0].id, "gl-1");
  EXPECT_FALSE(groups[0].ownedByBridgeHome);
  EXPECT_TRUE(groups[0].state.on);
  EXPECT_EQ(groups[0].state.brightness, 40);
  EXPECT_TRUE(groups[1].ownedByBridgeHome);
  EXPECT_FALSE(groups[2].state.on);
  EXPECT_EQ(groups[2].state.brightness, 0);

  const hue::GroupedLight* found = hue::findGroupedLight(groups, count, "gl-2");
  ASSERT_NE(found, nullptr);
  EXPECT_STREQ(found->id, "gl-2");
  EXPECT_EQ(hue::findGroupedLight(groups, count, "missing"), nullptr);
  EXPECT_EQ(hue::findGroupedLight(groups, count, ""), nullptr);
}

TEST(HueGroupedLightList, CapacityAndMalformed) {
  const char* reply = R"({"data":[{"id":"a","on":{"on":true}},{"id":"b","on":{"on":true}}]})";
  hue::GroupedLight groups[1];
  size_t count = 0;
  ASSERT_TRUE(hue::parseGroupedLights(reply, std::strlen(reply), groups, 1, count));
  EXPECT_EQ(count, 1u);
  EXPECT_FALSE(hue::parseGroupedLights("nope", 4, groups, 1, count));
  EXPECT_FALSE(hue::parseGroupedLights(R"({"errors":[]})", 13, groups, 1, count));
}

TEST(HueScenes, PicksLatestRecallPerRoomAndPrefersActive) {
  hue::Room rooms[3] = {};
  std::strcpy(rooms[0].id, "room-a");
  std::strcpy(rooms[1].id, "room-b");
  std::strcpy(rooms[2].id, "room-c");
  const char* reply = R"({"errors":[],"data":[
    {"id":"s1","group":{"rid":"room-a","rtype":"room"},"metadata":{"name":"Relax"},"status":{"active":"inactive","last_recall":"2026-09-14T08:00:00Z"}},
    {"id":"s2","group":{"rid":"room-a","rtype":"room"},"metadata":{"name":"Bright"},"status":{"active":"inactive","last_recall":"2026-09-14T12:00:00Z"}},
    {"id":"s3","group":{"rid":"room-a","rtype":"room"},"metadata":{"name":"Older but active"},"status":{"active":"static","last_recall":"2026-09-01T00:00:00Z"}},
    {"id":"s4","group":{"rid":"room-b","rtype":"room"},"metadata":{"name":"No timestamp"},"status":{"active":"inactive"}},
    {"id":"s5","group":{"rid":"zone-x","rtype":"zone"},"metadata":{"name":"Zone scene"},"status":{"active":"static"}}]})";
  hue::RoomScene scenes[3];
  ASSERT_TRUE(hue::parseLastScenes(reply, std::strlen(reply), rooms, 3, scenes));
  EXPECT_STREQ(scenes[0].sceneId, "s3");
  EXPECT_TRUE(scenes[0].active);
  EXPECT_STREQ(scenes[0].name, "Older but active");
  EXPECT_STREQ(scenes[1].sceneId, "s4");
  EXPECT_FALSE(scenes[1].active);
  EXPECT_STREQ(scenes[2].sceneId, "");

  // Without an active scene the newest recall wins.
  const char* replyInactive = R"({"data":[
    {"id":"s1","group":{"rid":"room-a"},"metadata":{"name":"Relax"},"status":{"active":"inactive","last_recall":"2026-09-14T08:00:00Z"}},
    {"id":"s2","group":{"rid":"room-a"},"metadata":{"name":"Bright"},"status":{"active":"inactive","last_recall":"2026-09-14T12:00:00Z"}}]})";
  ASSERT_TRUE(hue::parseLastScenes(replyInactive, std::strlen(replyInactive), rooms, 3, scenes));
  EXPECT_STREQ(scenes[0].sceneId, "s2");
  EXPECT_FALSE(hue::parseLastScenes("[", 1, rooms, 3, scenes));
}

TEST(HueBodies, SceneRecall) {
  char body[64];
  hue::buildSceneRecallBody(body, sizeof(body));
  EXPECT_STREQ(body, R"({"recall":{"action":"active"}})");
  char path[96];
  hue::buildResourcePath(path, sizeof(path), "scene", "s1");
  EXPECT_STREQ(path, "/clip/v2/resource/scene/s1");
}

TEST(HueBodies, OnOffAndBrightness) {
  char body[96];
  hue::buildOnBody(body, sizeof(body), true);
  EXPECT_STREQ(body, R"({"on":{"on":true}})");
  hue::buildOnBody(body, sizeof(body), false);
  EXPECT_STREQ(body, R"({"on":{"on":false}})");
  hue::buildBrightnessBody(body, sizeof(body), 50, false);
  EXPECT_STREQ(body, R"({"dimming":{"brightness":50}})");
  hue::buildBrightnessBody(body, sizeof(body), 50, true);
  EXPECT_STREQ(body, R"({"on":{"on":true},"dimming":{"brightness":50}})");
}

TEST(HueBodies, BrightnessIsClamped) {
  EXPECT_EQ(hue::clampBrightness(0), 1);
  EXPECT_EQ(hue::clampBrightness(-5), 1);
  EXPECT_EQ(hue::clampBrightness(150), 100);
  EXPECT_EQ(hue::clampBrightness(42), 42);
  char body[64];
  hue::buildBrightnessBody(body, sizeof(body), 0, false);
  EXPECT_STREQ(body, R"({"dimming":{"brightness":1}})");
}

TEST(HueBodies, ResourcePath) {
  char path[96];
  hue::buildResourcePath(path, sizeof(path), "grouped_light", "gl-1");
  EXPECT_STREQ(path, "/clip/v2/resource/grouped_light/gl-1");
}

// ---------------------------------------------------------------- tado

TEST(TadoForms, DeviceAuthorize) {
  char form[160];
  tado::buildDeviceAuthorizeForm(form, sizeof(form));
  EXPECT_STREQ(form, "client_id=1bb50063-6b0c-4d11-bd99-387f4a91cc46&scope=offline_access");
}

TEST(TadoForms, DeviceTokenEncodesGrantTypeAndCode) {
  char form[320];
  ASSERT_GT(tado::buildDeviceTokenForm(form, sizeof(form), "abc/123+x=="), 0u);
  EXPECT_STREQ(form,
               "client_id=1bb50063-6b0c-4d11-bd99-387f4a91cc46"
               "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"
               "&device_code=abc%2F123%2Bx%3D%3D");
}

TEST(TadoForms, RefreshTokenForm) {
  char form[320];
  ASSERT_GT(tado::buildRefreshForm(form, sizeof(form), "rt-token_1.2~3"), 0u);
  EXPECT_STREQ(form, "client_id=1bb50063-6b0c-4d11-bd99-387f4a91cc46&grant_type=refresh_token&refresh_token=rt-token_1.2~3");
}

TEST(TadoForms, TruncatedFormIsReportedAsEmpty) {
  char form[40];
  EXPECT_EQ(tado::buildRefreshForm(form, sizeof(form), "this-will-not-fit"), 0u);
}

TEST(TadoDeviceCode, ParsesAllFieldsPreferringCompleteUri) {
  const char* reply = R"({"device_code":"dc-1","expires_in":300,"interval":5,"user_code":"ABCD-EFGH",
    "verification_uri":"https://login.tado.com/oauth2/device","verification_uri_complete":"https://login.tado.com/oauth2/device?user_code=ABCD-EFGH"})";
  tado::DeviceCode code;
  ASSERT_TRUE(tado::parseDeviceAuthorize(reply, std::strlen(reply), code));
  EXPECT_STREQ(code.deviceCode, "dc-1");
  EXPECT_STREQ(code.userCode, "ABCD-EFGH");
  EXPECT_STREQ(code.verificationUri, "https://login.tado.com/oauth2/device?user_code=ABCD-EFGH");
  EXPECT_EQ(code.intervalSec, 5);
  EXPECT_EQ(code.expiresInSec, 300u);
}

TEST(TadoDeviceCode, IntervalDefaultsWhenMissing) {
  const char* reply = R"({"device_code":"dc","user_code":"U","verification_uri":"https://x"})";
  tado::DeviceCode code;
  ASSERT_TRUE(tado::parseDeviceAuthorize(reply, std::strlen(reply), code));
  EXPECT_EQ(code.intervalSec, tado::kDefaultPollIntervalSec);
  EXPECT_STREQ(code.verificationUri, "https://x");
}

TEST(TadoDeviceCode, MissingFieldsFail) {
  const char* reply = R"({"user_code":"U"})";
  tado::DeviceCode code;
  EXPECT_FALSE(tado::parseDeviceAuthorize(reply, std::strlen(reply), code));
}

TEST(TadoToken, PendingAndOtherErrors) {
  tado::Tokens tokens;
  auto parse = [&](const char* body) { return tado::parseTokenResponse(400, body, std::strlen(body), tokens); };
  EXPECT_EQ(parse(R"({"error":"authorization_pending","error_description":"x"})"), tado::TokenResult::AuthorizationPending);
  EXPECT_EQ(parse(R"({"error":"slow_down"})"), tado::TokenResult::SlowDown);
  EXPECT_EQ(parse(R"({"error":"expired_token"})"), tado::TokenResult::ExpiredToken);
  EXPECT_EQ(parse(R"({"error":"access_denied"})"), tado::TokenResult::AccessDenied);
  EXPECT_EQ(parse(R"({"error":"invalid_grant"})"), tado::TokenResult::InvalidGrant);
  EXPECT_EQ(parse(R"({"error":"something_else"})"), tado::TokenResult::Malformed);
}

TEST(TadoToken, SuccessCopiesTokens) {
  const char* reply = R"({"access_token":"at.jwt","expires_in":599,"refresh_token":"rt-1","scope":"offline_access","token_type":"Bearer"})";
  tado::Tokens tokens;
  EXPECT_EQ(tado::parseTokenResponse(200, reply, std::strlen(reply), tokens), tado::TokenResult::Ok);
  EXPECT_STREQ(tokens.accessToken, "at.jwt");
  EXPECT_STREQ(tokens.refreshToken, "rt-1");
  EXPECT_EQ(tokens.expiresInSec, 599u);
}

TEST(TadoToken, SuccessWithoutRefreshTokenIsMalformed) {
  const char* reply = R"({"access_token":"at","expires_in":599})";
  tado::Tokens tokens;
  EXPECT_EQ(tado::parseTokenResponse(200, reply, std::strlen(reply), tokens), tado::TokenResult::Malformed);
  const char* garbage = "<html>";
  EXPECT_EQ(tado::parseTokenResponse(502, garbage, std::strlen(garbage), tokens), tado::TokenResult::Malformed);
}

TEST(TadoHome, ParsesFirstHomeId) {
  const char* reply = R"({"name":"K","email":"k@x","homes":[{"id":123456,"name":"Home"},{"id":7,"name":"Other"}]})";
  int32_t homeId = 0;
  ASSERT_TRUE(tado::parseHomeId(reply, std::strlen(reply), homeId));
  EXPECT_EQ(homeId, 123456);
}

TEST(TadoHome, EmptyHomesFails) {
  const char* reply = R"({"homes":[]})";
  int32_t homeId = 0;
  EXPECT_FALSE(tado::parseHomeId(reply, std::strlen(reply), homeId));
}

TEST(TadoZones, KeepsOnlyHeatingZones) {
  const char* reply = R"([
    {"id":1,"name":"Living","type":"HEATING","devices":[{"serialNo":"x"}]},
    {"id":2,"name":"Water","type":"HOT_WATER"},
    {"id":3,"name":"Office","type":"HEATING"},
    {"id":4,"name":"AC","type":"AIR_CONDITIONING"}])";
  tado::Zone zones[tado::kMaxZones];
  size_t count = 0;
  ASSERT_TRUE(tado::parseZones(reply, std::strlen(reply), zones, tado::kMaxZones, count));
  ASSERT_EQ(count, 2u);
  EXPECT_EQ(zones[0].id, 1);
  EXPECT_STREQ(zones[0].name, "Living");
  EXPECT_EQ(zones[1].id, 3);
  EXPECT_STREQ(zones[1].name, "Office");
}

TEST(TadoZones, NonArrayFails) {
  tado::Zone zones[2];
  size_t count = 0;
  const char* reply = R"({"errors":[{"code":"unauthorized"}]})";
  EXPECT_FALSE(tado::parseZones(reply, std::strlen(reply), zones, 2, count));
}

TEST(TadoZoneState, OnSchedule) {
  const char* reply = R"({"tadoMode":"HOME","setting":{"type":"HEATING","power":"ON","temperature":{"celsius":19.0,"fahrenheit":66.2}},
    "overlayType":null,"overlay":null,"nextTimeBlock":{"start":"2026-09-14T20:00:00Z"},
    "activityDataPoints":{"heatingPower":{"percentage":0.0}},
    "sensorDataPoints":{"insideTemperature":{"celsius":21.3,"fahrenheit":70.3,"type":"TEMPERATURE"},"humidity":{"type":"PERCENTAGE","percentage":48.5}}})";
  tado::ZoneState state;
  ASSERT_TRUE(tado::parseZoneState(reply, std::strlen(reply), state));
  EXPECT_TRUE(state.powerOn);
  EXPECT_TRUE(state.hasTarget);
  EXPECT_FLOAT_EQ(state.targetCelsius, 19.0f);
  EXPECT_TRUE(state.hasInside);
  EXPECT_FLOAT_EQ(state.insideCelsius, 21.3f);
  EXPECT_TRUE(state.hasHumidity);
  EXPECT_FLOAT_EQ(state.humidityPct, 48.5f);
  EXPECT_FALSE(state.hasOverlay);
}

TEST(TadoZoneState, WithOverlay) {
  const char* reply = R"({"setting":{"type":"HEATING","power":"ON","temperature":{"celsius":22.5}},
    "overlayType":"MANUAL","overlay":{"type":"MANUAL","setting":{"type":"HEATING","power":"ON","temperature":{"celsius":22.5}},"termination":{"type":"TADO_MODE"}},
    "sensorDataPoints":{"insideTemperature":{"celsius":20.0}}})";
  tado::ZoneState state;
  ASSERT_TRUE(tado::parseZoneState(reply, std::strlen(reply), state));
  EXPECT_TRUE(state.hasOverlay);
  EXPECT_FLOAT_EQ(state.targetCelsius, 22.5f);
  EXPECT_FALSE(state.hasHumidity);
}

TEST(TadoZoneState, PowerOffHasNoTarget) {
  const char* reply = R"({"setting":{"type":"HEATING","power":"OFF","temperature":null},"overlay":null,
    "sensorDataPoints":{"insideTemperature":{"celsius":18.2},"humidity":{"percentage":60.0}}})";
  tado::ZoneState state;
  ASSERT_TRUE(tado::parseZoneState(reply, std::strlen(reply), state));
  EXPECT_FALSE(state.powerOn);
  EXPECT_FALSE(state.hasTarget);
  EXPECT_FLOAT_EQ(state.insideCelsius, 18.2f);
}

TEST(TadoZoneState, MissingSettingFails) {
  const char* reply = R"({"sensorDataPoints":{}})";
  tado::ZoneState state;
  EXPECT_FALSE(tado::parseZoneState(reply, std::strlen(reply), state));
}

TEST(TadoSteps, RoundToNearestHalfAndClamp) {
  EXPECT_FLOAT_EQ(tado::roundToStep(19.24f), 19.0f);
  EXPECT_FLOAT_EQ(tado::roundToStep(19.26f), 19.5f);
  EXPECT_FLOAT_EQ(tado::roundToStep(19.75f), 20.0f);
  EXPECT_FLOAT_EQ(tado::roundToStep(4.9f), 5.0f);
  EXPECT_FLOAT_EQ(tado::roundToStep(25.3f), 25.0f);
}

TEST(TadoSteps, StepTargetClampsAtRangeEnds) {
  EXPECT_FLOAT_EQ(tado::stepTarget(25.0f, 0.5f), 25.0f);
  EXPECT_FLOAT_EQ(tado::stepTarget(5.0f, -0.5f), 5.0f);
  EXPECT_FLOAT_EQ(tado::stepTarget(19.0f, 0.5f), 19.5f);
  EXPECT_FLOAT_EQ(tado::stepTarget(19.0f, -0.5f), 18.5f);
}

TEST(TadoBodies, OverlayBodyRoundsAndUsesNextTimeBlock) {
  char body[200];
  ASSERT_GT(tado::buildOverlayBody(body, sizeof(body), 19.5f), 0u);
  EXPECT_STREQ(body,
               R"({"setting":{"type":"HEATING","power":"ON","temperature":{"celsius":19.5}},)"
               R"("termination":{"typeSkillBasedApp":"NEXT_TIME_BLOCK"}})");
  tado::buildOverlayBody(body, sizeof(body), 19.26f);
  EXPECT_NE(std::strstr(body, R"("celsius":19.5)"), nullptr);
}

TEST(TadoBodies, HeatingOffOverlayHasNoTemperature) {
  char body[200];
  ASSERT_GT(tado::buildHeatingOffOverlayBody(body, sizeof(body)), 0u);
  EXPECT_STREQ(body,
               R"({"setting":{"type":"HEATING","power":"OFF"},)"
               R"("termination":{"typeSkillBasedApp":"NEXT_TIME_BLOCK"}})");
  char tiny[8];
  EXPECT_EQ(tado::buildHeatingOffOverlayBody(tiny, sizeof(tiny)), sizeof(tiny) - 1);
}

TEST(TadoBodies, Paths) {
  char path[96];
  tado::buildZonesPath(path, sizeof(path), 123456);
  EXPECT_STREQ(path, "/homes/123456/zones");
  tado::buildZoneStatePath(path, sizeof(path), 123456, 3);
  EXPECT_STREQ(path, "/homes/123456/zones/3/state");
  tado::buildZoneOverlayPath(path, sizeof(path), 123456, 3);
  EXPECT_STREQ(path, "/homes/123456/zones/3/overlay");
}

}  // namespace
