#include "TadoZonesActivity.h"

#if CROSSINK_APP_CAP_HOME_CONTROL

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HomeControlStore.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "HomeControlTileScreen.h"
#include "MappedInputManager.h"
#include "components/TileGrid.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/homeControlIcons.h"
#include "fontIds.h"
#include "util/QrUtils.h"

namespace {
constexpr unsigned long SLOW_DOWN_EXTRA_MS = 5000;  // RFC 8628: add 5 s when told to slow down
constexpr unsigned long DEFAULT_DEVICE_CODE_LIFETIME_MS = 300000;
constexpr unsigned long COUNTDOWN_REDRAW_MS = 10000;  // e-ink: refresh the countdown sparingly
constexpr size_t BODY_BUFFER_PSRAM = 32 * 1024;
constexpr size_t BODY_BUFFER_FALLBACK = 12 * 1024;

TouchActionButtons::Layout touchActionLayout(const GfxRenderer& renderer, const uint8_t buttonCount) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int totalHeight =
      TouchActionButtons::kDefaultHeight * buttonCount + TouchActionButtons::kDefaultGap * (buttonCount - 1);
  const Rect container{screen.x + metrics.contentSidePadding,
                       screen.y + screen.height - metrics.verticalSpacing - totalHeight,
                       std::max(1, screen.width - metrics.contentSidePadding * 2), totalHeight};
  return TouchActionButtons::vertical(container, buttonCount);
}

int tappedAction(MappedInputManager& mappedInput, const TouchActionButtons::Layout& actions, bool& consumed) {
  consumed = false;
  if (!mappedInput.hasTouch() || actions.count == 0) return -1;
  int touched = -1;
  const auto touch = mappedInput.rowTouch(
      touched, actions.buttons[0].y, TouchActionButtons::kDefaultHeight + TouchActionButtons::kDefaultGap,
      actions.count, actions.buttons[0].x, actions.buttons[0].x + actions.buttons[0].width, actions.buttons[0].height);
  if (touch == MappedInputManager::RowTouch::Down) {
    consumed = true;
    return -1;
  }
  if (touch == MappedInputManager::RowTouch::Tap) {
    consumed = true;
    return touched;
  }
  return -1;
}

const char* messageFor(const TadoClient::Error error) {
  switch (error) {
    case TadoClient::Error::LowMemory:
      return tr(STR_HOME_CONTROL_LOW_MEMORY);
    case TadoClient::Error::Network:
      return tr(STR_HOME_CONTROL_NETWORK_ERROR);
    case TadoClient::Error::BadResponse:
      return tr(STR_HOME_CONTROL_BAD_RESPONSE);
    case TadoClient::Error::Expired:
      return tr(STR_TADO_CODE_EXPIRED);
    case TadoClient::Error::Denied:
      return tr(STR_TADO_ACCESS_DENIED);
    case TadoClient::Error::NotPaired:
      return tr(STR_TADO_TOKEN_REJECTED);
    default:
      return tr(STR_HOME_CONTROL_HTTP_ERROR);
  }
}

// "21.3°" style with one decimal; tado reports floats.
void formatCelsius(char* out, const size_t cap, const float celsius) {
  std::snprintf(out, cap, "%.1f°", static_cast<double>(celsius));
}
}  // namespace

TadoZonesActivity::TadoZonesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("TadoZones", renderer, mappedInput) {}

bool TadoZonesActivity::preventAutoSleep() {
  return state == State::SIGNING_IN || state == State::DEVICE_CODE || state == State::LOADING_ZONES ||
         state == State::APPLYING;
}

void TadoZonesActivity::setState(const State next) {
  RenderLock lock(*this);
  state = next;
}

bool TadoZonesActivity::allocateNetworkResources() {
#ifdef SIMULATOR
  body = makeInternalByteBufferNoThrow(BODY_BUFFER_PSRAM);
  size_t cap = BODY_BUFFER_PSRAM;
#else
  body = makePsramByteBufferNoThrow(BODY_BUFFER_PSRAM);
  size_t cap = BODY_BUFFER_PSRAM;
#endif
  if (!body) {
    LOG_ERR("HC", "PSRAM body buffer failed, falling back to internal %u bytes",
            static_cast<unsigned>(BODY_BUFFER_FALLBACK));
    body = makeInternalByteBufferNoThrow(BODY_BUFFER_FALLBACK);
    cap = BODY_BUFFER_FALLBACK;
  }
  if (!body) {
    LOG_ERR("HC", "OOM: tado response buffer");
    return false;
  }
  session = makeUniqueNoThrow<homecontrol::HttpSession>();
  if (!session) {
    LOG_ERR("HC", "OOM: tado HTTP session");
    return false;
  }
  client = makeUniqueNoThrow<TadoClient>(*session, body.get(), cap);
  if (!client) {
    LOG_ERR("HC", "OOM: tado client");
    return false;
  }
  return true;
}

void TadoZonesActivity::onEnter() {
  Activity::onEnter();
  selectedZone = 0;
  page = 0;
  zoneCount = 0;

  if (!allocateNetworkResources()) {
    errorMessage = tr(STR_HOME_CONTROL_LOW_MEMORY);
    setState(State::ERROR);
    requestUpdate();
    return;
  }

  if (HOME_CONTROL_STORE.hasTadoRefreshToken()) {
    signIn();
  } else {
    startDeviceAuth();
  }
}

void TadoZonesActivity::onExit() {
  Activity::onExit();
  client.reset();
  session.reset();
  body.reset();
}

void TadoZonesActivity::failWith(const TadoClient::Error error) {
  LOG_ERR("HC", "tado screen error: %s", TadoClient::errorName(error));
  errorMessage = messageFor(error);
  setState(State::ERROR);
  requestUpdate();
}

void TadoZonesActivity::signIn() {
  setState(State::SIGNING_IN);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  const TadoClient::Error error = client->ensureAccessToken();
  if (error == TadoClient::Error::Ok) {
    loadZones();
    return;
  }
  if (error == TadoClient::Error::NotPaired) {
    HOME_CONTROL_STORE.clearTado();
    if (!HOME_CONTROL_STORE.saveToFile()) LOG_ERR("HC", "Could not clear tado tokens");
    startDeviceAuth();
    return;
  }
  failWith(error);
}

void TadoZonesActivity::startDeviceAuth() {
  setState(State::SIGNING_IN);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  const TadoClient::Error error = client->startDeviceAuth(deviceCode);
  if (error != TadoClient::Error::Ok) {
    failWith(error);
    return;
  }
  const unsigned long now = millis();
  pollIntervalMs = static_cast<unsigned long>(deviceCode.intervalSec) * 1000UL;
  nextPollAtMs = now + pollIntervalMs;
  deviceCodeDeadlineMs =
      now + (deviceCode.expiresInSec > 0 ? deviceCode.expiresInSec * 1000UL : DEFAULT_DEVICE_CODE_LIFETIME_MS);
  lastCountdownRenderMs = now;
  setState(State::DEVICE_CODE);
  requestUpdate();
}

void TadoZonesActivity::deviceCodeTick() {
  const unsigned long now = millis();
  if (static_cast<long>(now - deviceCodeDeadlineMs) >= 0) {
    errorMessage = tr(STR_TADO_CODE_EXPIRED);
    setState(State::ERROR);
    requestUpdate();
    return;
  }
  if (static_cast<long>(now - nextPollAtMs) < 0) {
    if (now - lastCountdownRenderMs >= COUNTDOWN_REDRAW_MS) {
      lastCountdownRenderMs = now;
      requestUpdate();
    }
    return;
  }
  nextPollAtMs = now + pollIntervalMs;

  switch (client->pollDeviceToken(deviceCode.deviceCode)) {
    case TadoClient::Error::Ok:
      loadZones();
      return;
    case TadoClient::Error::Pending:
      return;
    case TadoClient::Error::SlowDown:
      pollIntervalMs += SLOW_DOWN_EXTRA_MS;
      nextPollAtMs = now + pollIntervalMs;
      return;
    case TadoClient::Error::Expired:
      failWith(TadoClient::Error::Expired);
      return;
    case TadoClient::Error::Denied:
      failWith(TadoClient::Error::Denied);
      return;
    case TadoClient::Error::Network:
      return;  // transient; keep polling until the code expires
    default:
      failWith(TadoClient::Error::BadResponse);
      return;
  }
}

bool TadoZonesActivity::fetchZoneState(const int index) {
  tado::ZoneState fresh;
  const TadoClient::Error error = client->getZoneState(homeId, zones[index].id, fresh);
  if (error != TadoClient::Error::Ok) {
    zoneStateKnown[index] = false;
    return error == TadoClient::Error::Http || error == TadoClient::Error::BadResponse;  // per-zone, keep going
  }
  zoneStates[index] = fresh;
  zoneStateKnown[index] = true;
  return true;
}

void TadoZonesActivity::loadZones() {
  setState(State::LOADING_ZONES);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  TadoClient::Error error = client->fetchHomeId(homeId);
  if (error != TadoClient::Error::Ok) {
    failWith(error);
    return;
  }
  size_t count = 0;
  error = client->listZones(homeId, zones, tado::kMaxZones, count);
  if (error != TadoClient::Error::Ok) {
    failWith(error);
    return;
  }
  // One GET per zone over the kept-alive socket so the list can show
  // current and target temperatures without opening each zone.
  for (size_t i = 0; i < count; ++i) {
    if (!fetchZoneState(static_cast<int>(i))) {
      failWith(TadoClient::Error::Network);
      return;
    }
  }
  {
    RenderLock lock(*this);
    zoneCount = count;
    for (size_t i = 0; i < count; ++i) formatZoneLines(static_cast<int>(i));
    selectedZone = 0;
    page = 0;
    state = State::ZONES;
  }
  requestUpdate();
}

void TadoZonesActivity::formatZoneLines(const int index) {
  char* line1 = zoneLine1[index];
  char* line2 = zoneLine2[index];
  if (!zoneStateKnown[index]) {
    std::snprintf(line1, kLineLen, "%s", tr(STR_HOME_CONTROL_NETWORK_ERROR));
    line2[0] = '\0';
    return;
  }
  const tado::ZoneState& s = zoneStates[index];
  char value[16] = "--";
  if (s.hasInside) formatCelsius(value, sizeof(value), s.insideCelsius);
  std::snprintf(line1, kLineLen, "%s %s", tr(STR_TADO_CURRENT), value);
  if (!s.powerOn) {
    std::snprintf(line2, kLineLen, "%s", tr(STR_TADO_HEATING_OFF));
    return;
  }
  std::snprintf(value, sizeof(value), "--");
  if (s.hasTarget) formatCelsius(value, sizeof(value), s.targetCelsius);
  std::snprintf(line2, kLineLen, "%s %s", s.hasOverlay ? tr(STR_TADO_MANUAL) : tr(STR_TADO_TARGET), value);
}

// Re-reads one zone after a write and refreshes its tile text.
void TadoZonesActivity::refreshZone(const int index) {
  fetchZoneState(index);
  RenderLock lock(*this);
  formatZoneLines(index);
}

void TadoZonesActivity::openZone(const int index) {
  if (index < 0 || index >= static_cast<int>(zoneCount)) return;
  {
    RenderLock lock(*this);
    selectedZone = index;
    state = State::ZONE_DETAIL;
  }
  requestUpdate();
}

void TadoZonesActivity::applyDetailAction(const DetailAction action) {
  const tado::ZoneState& current = zoneStates[selectedZone];
  setState(State::APPLYING);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  TadoClient::Error error = TadoClient::Error::Ok;
  const int32_t zoneId = zones[selectedZone].id;
  switch (action) {
    case DetailAction::WARMER:
    case DetailAction::COOLER: {
      // Start from the active target; when heating is off, from the room temperature.
      const float base = (current.powerOn && current.hasTarget) ? current.targetCelsius
                         : current.hasInside                    ? current.insideCelsius
                                                                : tado::kMinCelsius;
      const float delta = action == DetailAction::WARMER ? tado::kStepCelsius : -tado::kStepCelsius;
      error = client->setOverlay(homeId, zoneId, tado::stepTarget(base, delta));
      break;
    }
    case DetailAction::RESUME:
      error = client->clearOverlay(homeId, zoneId);
      break;
  }
  if (error != TadoClient::Error::Ok) {
    failWith(error);
    return;
  }
  refreshZone(selectedZone);
  setState(State::ZONE_DETAIL);
  requestUpdate();
}

// tado has no home-wide endpoint, so bulk actions loop the zones over the
// kept-alive socket: one PUT/DELETE and one state read per zone.
void TadoZonesActivity::applyBulkAction(const BulkAction action) {
  setState(State::APPLYING);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  for (size_t i = 0; i < zoneCount; ++i) {
    const int32_t zoneId = zones[i].id;
    const TadoClient::Error error =
        action == BulkAction::ALL_OFF ? client->setHeatingOff(homeId, zoneId) : client->clearOverlay(homeId, zoneId);
    if (error != TadoClient::Error::Ok) {
      failWith(error);
      return;
    }
  }
  for (size_t i = 0; i < zoneCount; ++i) refreshZone(static_cast<int>(i));
  setState(State::ZONES);
  requestUpdate();
}

void TadoZonesActivity::handleBack() {
  if (state == State::ZONE_DETAIL) {
    setState(State::ZONES);
    mappedInput.suppressNextBackRelease();
    requestUpdate();
    return;
  }
  finishAfterBackPress();
}

void TadoZonesActivity::handleErrorInput() {
  const auto actions = touchActionLayout(renderer, 2);
  bool consumed = false;
  const int tapped = tappedAction(mappedInput, actions, consumed);
  if (tapped == 0 || mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    mappedInput.suppressNextConfirmRelease();
    if (HOME_CONTROL_STORE.hasTadoRefreshToken()) {
      signIn();
    } else {
      startDeviceAuth();
    }
  } else if (tapped == 1) {
    HOME_CONTROL_STORE.clearTado();
    if (!HOME_CONTROL_STORE.saveToFile()) LOG_ERR("HC", "Could not clear tado tokens");
    startDeviceAuth();
  }
}

void TadoZonesActivity::handleDetailInput() {
  const auto actions = touchActionLayout(renderer, kDetailActionCount);
  bool consumed = false;
  const int tapped = tappedAction(mappedInput, actions, consumed);
  if (tapped >= 0 && tapped < kDetailActionCount) {
    applyDetailAction(static_cast<DetailAction>(tapped));
    return;
  }
  if (consumed) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    mappedInput.suppressNextConfirmRelease();
    applyDetailAction(DetailAction::RESUME);
    return;
  }
  buttonNavigator.onNext([this] { applyDetailAction(DetailAction::COOLER); });
  buttonNavigator.onPrevious([this] { applyDetailAction(DetailAction::WARMER); });
}

void TadoZonesActivity::handleZonesInput() {
  const int count = static_cast<int>(zoneCount);
  const auto geometry = HomeControlTileScreen::layout(renderer, mappedInput, count, page, kBulkActionCount);
  const auto event = HomeControlTileScreen::pollInput(mappedInput, buttonNavigator, geometry, count, selectedZone);
  switch (event.kind) {
    case HomeControlTileScreen::EventKind::OpenTile:
      openZone(event.value);
      break;
    case HomeControlTileScreen::EventKind::Action:
      if (event.value >= 0 && event.value < kBulkActionCount) applyBulkAction(static_cast<BulkAction>(event.value));
      break;
    case HomeControlTileScreen::EventKind::Select: {
      RenderLock lock(*this);
      selectedZone = event.value;
      page = geometry.grid.pageSize > 0 ? selectedZone / geometry.grid.pageSize : 0;
      requestUpdate();
      break;
    }
    case HomeControlTileScreen::EventKind::PageDelta: {
      const int next = std::max(0, std::min(page + event.value, geometry.grid.pageCount - 1));
      if (next == page) break;
      RenderLock lock(*this);
      page = next;
      selectedZone = std::min(count - 1, page * geometry.grid.pageSize);
      requestUpdate();
      break;
    }
    case HomeControlTileScreen::EventKind::None:
      break;
  }
}

void TadoZonesActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    handleBack();
    return;
  }
  switch (state) {
    case State::DEVICE_CODE:
      deviceCodeTick();
      break;
    case State::ZONES:
      handleZonesInput();
      break;
    case State::ZONE_DETAIL:
      handleDetailInput();
      break;
    case State::ERROR:
      handleErrorInput();
      break;
    case State::SIGNING_IN:
    case State::LOADING_ZONES:
    case State::APPLYING:
      break;
  }
}

void TadoZonesActivity::renderZones() {
  const int count = static_cast<int>(zoneCount);
  const auto geometry = HomeControlTileScreen::layout(renderer, mappedInput, count, page, kBulkActionCount);
  const bool showSelection = !mappedInput.hasTouch();
  for (int i = 0; i < geometry.grid.count; ++i) {
    const int index = geometry.grid.firstIndex + i;
    TileGrid::Content content;
    content.title = zones[index].name;
    content.line1 = zoneLine1[index];
    content.line2 = zoneLine2[index];
    content.icon = &icon_temperature_32;
    // Filled tiles mark manual overrides, so a glance shows what deviates from the schedule.
    content.filled = zoneStateKnown[index] && zoneStates[index].hasOverlay;
    TileGrid::drawTile(renderer, geometry.grid.tiles[i], content, showSelection && index == selectedZone, UI_10_FONT_ID,
                       UI_10_FONT_ID);
  }
  const char* labels[] = {tr(STR_TADO_ALL_OFF), tr(STR_TADO_RESUME_ALL)};
  HomeControlTileScreen::drawChrome(renderer, geometry, labels, mappedInput.hasTouch());
}

void TadoZonesActivity::renderMessage(const char* title, const char* body, const bool showRetry) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const int top = std::max(header.y + header.height + metrics.verticalSpacing * 2, screen.y + screen.height / 3);
  renderer.drawCenteredText(UI_10_FONT_ID, top, title, true, EpdFontFamily::BOLD);
  if (body != nullptr) {
    const Rect textArea{screen.x + metrics.contentSidePadding, screen.y, screen.width - metrics.contentSidePadding * 2,
                        screen.height};
    UITheme::drawCenteredWrappedText(renderer, textArea, UI_10_FONT_ID, top + lineHeight + 10, body, 4, true,
                                     EpdFontFamily::REGULAR, 4);
  }
  if (showRetry && mappedInput.hasTouch()) {
    const auto actions = touchActionLayout(renderer, 2);
    const char* labels[] = {tr(STR_RETRY), tr(STR_TADO_SIGN_OUT)};
    TouchActionButtons::draw(renderer, actions, labels, 0, -1, UI_10_FONT_ID);
  }
}

void TadoZonesActivity::renderDeviceCode() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  int y = header.y + header.height + metrics.verticalSpacing;

  renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_TADO_SCAN_QR));
  y += lineHeight + metrics.verticalSpacing;

  // The QR carries verification_uri_complete, so the phone lands on the
  // approval page with the code already filled in. Size it to what is left
  // above the code and countdown lines.
  const int reservedBelow =
      (lineHeight + metrics.verticalSpacing) * 4 + renderer.getLineHeight(UI_12_FONT_ID) + metrics.buttonHintsHeight;
  const int available = screen.y + screen.height - y - reservedBelow;
  const int qrSide = std::max(120, std::min(available, screen.width - metrics.contentSidePadding * 4));
  const Rect qrBounds{screen.x + (screen.width - qrSide) / 2, y, qrSide, qrSide};
  QrUtils::drawQrCode(renderer, qrBounds, deviceCode.verificationUri);
  y += qrSide + metrics.verticalSpacing;

  // Fallback for typing by hand: host and path only, the code goes in separately.
  char shortUrl[96];
  const char* hostStart = std::strstr(deviceCode.verificationUri, "://");
  hostStart = hostStart != nullptr ? hostStart + 3 : deviceCode.verificationUri;
  const char* query = std::strchr(hostStart, '?');
  const size_t shortLen = query != nullptr ? static_cast<size_t>(query - hostStart) : std::strlen(hostStart);
  std::snprintf(shortUrl, sizeof(shortUrl), "%.*s", static_cast<int>(std::min(shortLen, sizeof(shortUrl) - 1)),
                hostStart);
  renderer.drawCenteredText(UI_10_FONT_ID, y, shortUrl);
  y += lineHeight + metrics.verticalSpacing;

  renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_TADO_ENTER_CODE));
  y += lineHeight + metrics.verticalSpacing / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, y, deviceCode.userCode, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  const unsigned long now = millis();
  const long remainingMs = static_cast<long>(deviceCodeDeadlineMs - now);
  char line[64];
  std::snprintf(line, sizeof(line), "%s %ld s", tr(STR_TADO_WAITING_APPROVAL),
                remainingMs > 0 ? remainingMs / 1000 : 0L);
  renderer.drawCenteredText(UI_10_FONT_ID, y, line);
}

void TadoZonesActivity::renderDetail() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const tado::ZoneState& s = zoneStates[selectedZone];
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  int y = header.y + header.height + metrics.verticalSpacing * 2;
  char value[16];
  char line[64];

  if (!zoneStateKnown[selectedZone]) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_HOME_CONTROL_NETWORK_ERROR));
    return;
  }

  if (s.hasInside) {
    formatCelsius(value, sizeof(value), s.insideCelsius);
    std::snprintf(line, sizeof(line), "%s %s", tr(STR_TADO_CURRENT), value);
    renderer.drawCenteredText(UI_12_FONT_ID, y, line, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  }
  if (s.powerOn && s.hasTarget) {
    formatCelsius(value, sizeof(value), s.targetCelsius);
    std::snprintf(line, sizeof(line), "%s %s", tr(STR_TADO_TARGET), value);
  } else {
    std::snprintf(line, sizeof(line), "%s", tr(STR_TADO_HEATING_OFF));
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y, line);
  y += lineHeight + metrics.verticalSpacing;
  if (s.hasHumidity) {
    std::snprintf(line, sizeof(line), "%s %.0f%%", tr(STR_TADO_HUMIDITY), static_cast<double>(s.humidityPct));
    renderer.drawCenteredText(UI_10_FONT_ID, y, line);
    y += lineHeight + metrics.verticalSpacing;
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y, s.hasOverlay ? tr(STR_TADO_UNTIL_NEXT_BLOCK) : tr(STR_TADO_ON_SCHEDULE));

  if (mappedInput.hasTouch()) {
    const auto actions = touchActionLayout(renderer, kDetailActionCount);
    const char* labels[] = {tr(STR_TADO_WARMER), tr(STR_TADO_COOLER), tr(STR_TADO_RESUME_SCHEDULE)};
    TouchActionButtons::draw(renderer, actions, labels, -1, -1, UI_10_FONT_ID);
  }
}

void TadoZonesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const char* title = state == State::ZONE_DETAIL || state == State::APPLYING ? zones[selectedZone].name : tr(STR_TADO);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, title, false);
  } else {
    GUI.drawHeader(renderer, header, title);
  }

  const char* hint2 = "";
  const char* hint3 = "";
  const char* hint4 = "";
  switch (state) {
    case State::ZONES:
      if (zoneCount == 0) {
        renderMessage(tr(STR_TADO_ZONES), tr(STR_TADO_NO_ZONES), false);
      } else {
        renderZones();
        hint2 = tr(STR_SELECT);
        hint3 = tr(STR_DIR_UP);
        hint4 = tr(STR_DIR_DOWN);
      }
      break;
    case State::ZONE_DETAIL:
      renderDetail();
      hint2 = tr(STR_TADO_RESUME_SCHEDULE);
      hint3 = tr(STR_TADO_WARMER);
      hint4 = tr(STR_TADO_COOLER);
      break;
    case State::APPLYING:
      renderMessage(tr(STR_HOME_CONTROL_APPLYING), nullptr, false);
      break;
    case State::DEVICE_CODE:
      renderDeviceCode();
      break;
    case State::SIGNING_IN:
      renderMessage(tr(STR_HOME_CONTROL_SIGNING_IN), nullptr, false);
      break;
    case State::LOADING_ZONES:
      renderMessage(tr(STR_HOME_CONTROL_LOADING_ZONES), nullptr, false);
      break;
    case State::ERROR:
      renderMessage(tr(STR_TADO), errorMessage, true);
      hint2 = tr(STR_RETRY);
      break;
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), hint2, hint3, hint4);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(screenTransitionRefresh.modeFor(static_cast<uint8_t>(state)));
}

#endif  // CROSSINK_APP_CAP_HOME_CONTROL
