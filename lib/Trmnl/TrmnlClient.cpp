// TRMNL BYOS transport. The request shape (device headers, /api/setup then
// /api/display, TLS heap floor) is ported from cross-trmnl
// (https://github.com/wolodarskij/cross-trmnl, src/network/TrmnlClient.cpp),
// MIT License, Copyright (c) 2025 Dave Allie.
#include "TrmnlClient.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <AppVersion.h>
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

namespace trmnl {

namespace {

// cross-trmnl's floor: a wolfSSL handshake needs this much free and contiguous heap.
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;
constexpr uint32_t JSON_TIMEOUT_MS = 10000;
// /api/setup and /api/display bodies are a few hundred bytes. A fixed cap
// bounds the read instead of growing a String to whatever the server sends.
constexpr size_t BODY_CAP = 2048;
// The panel's landscape size, which TRMNL servers render for.
constexpr int PANEL_WIDTH = 800;
constexpr int PANEL_HEIGHT = 480;

bool isHttps(const char* url) { return std::strncmp(url, "https://", 8) == 0; }

uint16_t batteryMillivolts() {
#ifdef SIMULATOR
  return 4200;  // the simulator's BatteryMonitor has no readMillivolts()
#else
  // Function-local like HalPowerManager's: BoardConfig::ACTIVE must be resolved first.
  static const BatteryMonitor battery;
  return battery.readMillivolts();
#endif
}

void addDeviceHeaders(freeink::SecureHttpClient& http, const char* apiKey) {
  char value[24];
  http.addHeader("Accept", "application/json");
  http.addHeader("ID", WiFi.macAddress().c_str());
  if (apiKey != nullptr && apiKey[0] != '\0') http.addHeader("Access-Token", apiKey);
  const uint16_t mv = batteryMillivolts();
  snprintf(value, sizeof(value), "%u.%02u", mv / 1000U, (mv % 1000U) / 10U);
  http.addHeader("Battery-Voltage", value);
  // Terminus answers 422 to anything but "X.Y.Z"; a build suffix is dropped.
  if (firmwareVersionCore(AppVersion::version(), value, sizeof(value))) http.addHeader("FW-Version", value);
  snprintf(value, sizeof(value), "%d", static_cast<int>(WiFi.RSSI()));
  http.addHeader("RSSI", value);
  snprintf(value, sizeof(value), "%d", PANEL_WIDTH);
  http.addHeader("Width", value);
  snprintf(value, sizeof(value), "%d", PANEL_HEIGHT);
  http.addHeader("Height", value);
  http.addHeader("Model", BoardConfig::ACTIVE.name);
}

// GET baseUrl + path into body (NUL-terminated). Returns the HTTP status or a
// negative kStatus* value. bodyLen is the number of bytes read.
int getJson(const char* baseUrl, const char* path, const char* apiKey, char* body, size_t& bodyLen) {
  bodyLen = 0;
  body[0] = '\0';
  char url[192];
  const int urlLen = snprintf(url, sizeof(url), "%s%s", baseUrl, path);
  if (urlLen < 0 || static_cast<size_t>(urlLen) >= sizeof(url)) {
    LOG_ERR("TRMNL", "URL too long: %s%s", baseUrl, path);
    return kStatusBadUrl;
  }
  if (!heapOkForUrl(url)) return kStatusLowMemory;

  freeink::SecureHttpClient http;
  // TODO(trmnl-https): no certificate check yet. See "HTTPS certificate
  // verification" under "Not yet specified" in .handovers/handover_log.md.
  if (isHttps(url)) http.setInsecure();
  http.setTimeout(JSON_TIMEOUT_MS);
  if (!http.begin(url)) {
    LOG_ERR("TRMNL", "Bad URL: %s", url);
    return kStatusBadUrl;
  }
  addDeviceHeaders(http, apiKey);

  bool tooLarge = false;
#ifdef SIMULATOR
  // The simulator's client has no streaming GET; bound the copy instead.
  const int status = http.GET();
  if (status > 0) {
    const String text = http.getString();
    if (text.length() >= BODY_CAP) {
      tooLarge = true;
    } else {
      bodyLen = text.length();
      std::memcpy(body, text.c_str(), bodyLen);
    }
  }
#else
  // One std::function per request, not per byte: it is the SDK's callback type.
  const auto onData = [body, &bodyLen, &tooLarge](const uint8_t* data, size_t len) {
    if (bodyLen + len >= BODY_CAP) {  // keep one byte for the NUL
      tooLarge = true;
      return false;
    }
    std::memcpy(body + bodyLen, data, len);
    bodyLen += len;
    return true;
  };
  const int status = http.GET(onData);
#endif
  http.end();
  body[bodyLen] = '\0';

  if (tooLarge) {
    LOG_ERR("TRMNL", "%s body exceeded %u bytes", path, static_cast<unsigned>(BODY_CAP));
    return kStatusBodyTooLarge;
  }
  if (status < 0) {
    LOG_ERR("TRMNL", "%s transport failure (%d)", path, status);
    return kStatusTransport;
  }
  LOG_DBG("TRMNL", "%s -> %d (%u bytes)", path, status, static_cast<unsigned>(bodyLen));
  return status;
}

}  // namespace

bool heapOkForUrl(const char* url) {
  if (!isHttps(url)) return true;
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAlloc = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_HEAP_FOR_TLS || maxAlloc < MIN_HEAP_FOR_TLS) {
    LOG_ERR("TRMNL", "Insufficient heap for TLS: %u free, %u max alloc", freeHeap, maxAlloc);
    return false;
  }
  return true;
}

int requestSetup(const char* baseUrl, SetupResult& out) {
  out = SetupResult{};
  // Heap, not stack or static: 2 KB exceeds the stack budget, and the buffer is
  // needed for one call per wake only.
  auto body = makeUniqueNoThrow<char[]>(BODY_CAP);
  if (!body) {
    LOG_ERR("TRMNL", "OOM for setup body");
    return kStatusLowMemory;
  }
  size_t len = 0;
  const int status = getJson(baseUrl, "/api/setup", nullptr, body.get(), len);
  if (status == 200 && !parseSetup(body.get(), len, out)) LOG_ERR("TRMNL", "/api/setup body has no usable api_key");
  return status;
}

int requestDisplay(const char* baseUrl, const char* apiKey, DisplayResult& out) {
  out = DisplayResult{};
  auto body = makeUniqueNoThrow<char[]>(BODY_CAP);  // same reasoning as requestSetup
  if (!body) {
    LOG_ERR("TRMNL", "OOM for display body");
    return kStatusLowMemory;
  }
  size_t len = 0;
  const int status = getJson(baseUrl, "/api/display", apiKey, body.get(), len);
  if ((status == 200 || status == 202) && len > 0 && !parseDisplay(body.get(), len, out)) {
    LOG_ERR("TRMNL", "/api/display body did not parse");
  }
  return status;
}

}  // namespace trmnl

#endif  // CROSSINK_APP_CAP_DASHBOARD
