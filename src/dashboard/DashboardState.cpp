#include "DashboardState.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "DashboardClock.h"
#include "DashboardImageStore.h"

namespace dashboard {

namespace {
// state.json is small and fixed-shape; 512 bytes leaves room for a 95-char filename.
constexpr size_t STATE_JSON_MAX = 512;

void copyString(char* dst, const size_t dstSize, const char* src) {
  strncpy(dst, src ? src : "", dstSize - 1);
  dst[dstSize - 1] = '\0';
}
}  // namespace

void DashboardState::load() {
  *this = DashboardState{};
  if (!Storage.exists(DashboardImageStore::STATE_JSON)) return;

  // Static: the wake path runs on the main task with a small stack budget.
  static char json[STATE_JSON_MAX];
  const size_t length = Storage.readFileToBuffer(DashboardImageStore::STATE_JSON, json, sizeof(json));
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, length);
  if (length == 0 || err) {
    LOG_ERR("DSH", "Corrupt state.json (%s); using defaults", err ? err.c_str() : "empty");
    return;
  }
  lastSuccessUtc = doc["lastSuccessUtc"] | 0u;
  firstFailureUtc = doc["firstFailureUtc"] | 0u;
  lastNtpSyncUtc = doc["lastNtpSyncUtc"] | 0u;
  copyString(lastFilename, sizeof(lastFilename), doc["lastFilename"] | "");
  copyString(lastReason, sizeof(lastReason), doc["lastReason"] | "");
  consecutiveFailures = doc["consecutiveFailures"] | 0;
  const uint8_t shown = doc["screenShows"] | 0;
  screenShows = shown <= static_cast<uint8_t>(Shown::FallbackFailures) ? static_cast<Shown>(shown) : Shown::Nothing;
  fastRefreshesSinceHalf = doc["fastRefreshesSinceHalf"] | 0;
  localFolderIndex = doc["localFolderIndex"] | 0;
}

bool DashboardState::save() const {
  if (!DashboardImageStore::ensureDirectory()) return false;
  JsonDocument doc;
  doc["lastSuccessUtc"] = lastSuccessUtc;
  doc["firstFailureUtc"] = firstFailureUtc;
  doc["lastNtpSyncUtc"] = lastNtpSyncUtc;
  doc["lastFilename"] = lastFilename;
  doc["lastReason"] = lastReason;
  doc["consecutiveFailures"] = consecutiveFailures;
  doc["screenShows"] = static_cast<uint8_t>(screenShows);
  doc["fastRefreshesSinceHalf"] = fastRefreshesSinceHalf;
  doc["localFolderIndex"] = localFolderIndex;

  static char json[STATE_JSON_MAX];
  const size_t length = serializeJson(doc, json, sizeof(json));
  if (length == 0 || length >= sizeof(json)) {
    LOG_ERR("DSH", "state.json does not fit its buffer");
    return false;
  }
  FsFile file;
  if (!Storage.openFileForWrite("DSH", DashboardImageStore::STATE_JSON, file)) return false;
  const size_t written = file.write(json, length);
  file.close();
  if (written != length) {
    LOG_ERR("DSH", "Short state.json write");
    return false;
  }
  return true;
}

void formatStatusLine(const DashboardState& state, char* buf, const size_t bufSize) {
  char time[8];
  switch (state.screenShows) {
    case Shown::FallbackQuiet:
      snprintf(buf, bufSize, "%s", tr(STR_DASHBOARD_STATUS_QUIET));
      return;
    case Shown::FallbackBattery:
      snprintf(buf, bufSize, "%s", tr(STR_DASHBOARD_STATUS_BATTERY));
      return;
    default:
      break;
  }
  if (state.consecutiveFailures > 0) {
    clock::formatLocalTime(state.firstFailureUtc, time, sizeof(time));
    snprintf(buf, bufSize, tr(STR_DASHBOARD_STATUS_FAILED), state.lastReason, time[0] ? time : "--:--");
    return;
  }
  if (state.lastSuccessUtc != 0) {
    clock::formatLocalTime(state.lastSuccessUtc, time, sizeof(time));
    snprintf(buf, bufSize, tr(STR_DASHBOARD_STATUS_LAST_UPDATE), time);
    return;
  }
  snprintf(buf, bufSize, "%s", tr(STR_DASHBOARD_STATUS_NEVER));
}

}  // namespace dashboard

#endif  // CROSSINK_APP_CAP_DASHBOARD
