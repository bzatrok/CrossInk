#include "HueRoomsActivity.h"

#if CROSSINK_APP_CAP_HOME_CONTROL

#include <Arduino.h>
#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <HomeControlStore.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "HomeControlTileScreen.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TileGrid.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/homeControlIcons.h"
#include "fontIds.h"

namespace {
// Hue answers "link button not pressed" until the button is pushed; poll
// gently and give the user a minute and a half to reach the bridge.
constexpr unsigned long PAIR_RETRY_MS = 2000;
constexpr unsigned long PAIR_TIMEOUT_MS = 90000;
// The scene list carries every light action of every scene and is by far the
// largest reply; PSRAM is plentiful on the X4 Pro. The internal fallback only
// fits rooms and states, so scenes degrade to plain on/off there.
constexpr size_t BODY_BUFFER_PSRAM = 160 * 1024;
constexpr size_t BODY_BUFFER_FALLBACK = 16 * 1024;

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

// Returns the tapped action index, or -1. Consumes touch-down so a press does
// not fall through to other handlers.
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

const char* messageFor(const HueClient::Error error) {
  switch (error) {
    case HueClient::Error::LowMemory:
      return tr(STR_HOME_CONTROL_LOW_MEMORY);
    case HueClient::Error::Network:
      return tr(STR_HOME_CONTROL_NETWORK_ERROR);
    case HueClient::Error::BadResponse:
      return tr(STR_HOME_CONTROL_BAD_RESPONSE);
    case HueClient::Error::Http:
      return tr(STR_HUE_PAIRING_REJECTED);
    default:
      return tr(STR_HOME_CONTROL_HTTP_ERROR);
  }
}
}  // namespace

HueRoomsActivity::HueRoomsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("HueRooms", renderer, mappedInput) {}

bool HueRoomsActivity::preventAutoSleep() {
  return state == State::SEARCHING || state == State::PAIRING || state == State::LOADING_ROOMS ||
         state == State::APPLYING;
}

void HueRoomsActivity::setState(const State next) {
  RenderLock lock(*this);
  state = next;
}

bool HueRoomsActivity::allocateNetworkResources() {
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
    LOG_ERR("HC", "OOM: Hue response buffer");
    return false;
  }
  session = makeUniqueNoThrow<homecontrol::HttpSession>();
  if (!session) {
    LOG_ERR("HC", "OOM: Hue HTTP session");
    return false;
  }
  client = makeUniqueNoThrow<HueClient>(*session, body.get(), cap);
  if (!client) {
    LOG_ERR("HC", "OOM: Hue client");
    return false;
  }
  return true;
}

void HueRoomsActivity::onEnter() {
  Activity::onEnter();
  selectedRoom = 0;
  page = 0;
  roomCount = 0;
  allLightsId[0] = '\0';

  if (!allocateNetworkResources()) {
    errorMessage = tr(STR_HOME_CONTROL_LOW_MEMORY);
    setState(State::ERROR);
    requestUpdate();
    return;
  }

  if (!HOME_CONTROL_STORE.hasHueBridge()) {
    discoverBridge();
  } else if (!HOME_CONTROL_STORE.hasHuePairing()) {
    startPairing();
  } else {
    loadRooms();
  }
}

void HueRoomsActivity::onExit() {
  Activity::onExit();
  // Reverse order of allocation: the client references the session and buffer.
  client.reset();
  session.reset();
  body.reset();
}

void HueRoomsActivity::discoverBridge() {
  setState(State::SEARCHING);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  std::string ip;
#ifdef SIMULATOR
  ip = "192.168.4.2";  // the simulator mock cannot query; fixtures answer any address
#else
  // Hue bridges advertise _hue._tcp. The query blocks for a few seconds, which
  // is fine on this dedicated screen; the keyboard remains the fallback.
  MDNS.end();
  if (MDNS.begin("crossink")) {
    const int found = MDNS.queryService("hue", "tcp");
    if (found > 0) {
      ip = MDNS.address(0).toString().c_str();
      LOG_INF("HC", "Hue bridge found via mDNS: %s (%d advertised)", ip.c_str(), found);
    } else {
      LOG_INF("HC", "No Hue bridge advertised via mDNS");
    }
    MDNS.end();
  } else {
    LOG_ERR("HC", "mDNS could not start; asking for the bridge address");
  }
#endif

  if (ip.empty()) {
    askForBridgeIp();
    return;
  }
  HOME_CONTROL_STORE.setHueBridgeIp(ip);
  HOME_CONTROL_STORE.clearHuePairing();
  if (!HOME_CONTROL_STORE.saveToFile()) LOG_ERR("HC", "Could not save Hue bridge address");
  startPairing();
}

void HueRoomsActivity::askForBridgeIp() {
  setState(State::NEED_IP);
  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_HUE_ENTER_BRIDGE_IP));
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      finish();
      return;
    }
    const std::string& ip = std::get<KeyboardResult>(result.data).text;
    if (ip.empty()) {
      finish();
      return;
    }
    HOME_CONTROL_STORE.setHueBridgeIp(ip);
    HOME_CONTROL_STORE.clearHuePairing();  // a new bridge needs its own key
    if (!HOME_CONTROL_STORE.saveToFile()) LOG_ERR("HC", "Could not save Hue bridge address");
    startPairing();
  });
}

void HueRoomsActivity::startPairing() {
  const unsigned long now = millis();
  nextPairAttemptAtMs = now;  // first attempt on the next loop tick
  pairingDeadlineMs = now + PAIR_TIMEOUT_MS;
  setState(State::PAIRING);
  requestUpdate();
}

void HueRoomsActivity::pairingTick() {
  const unsigned long now = millis();
  if (static_cast<long>(now - nextPairAttemptAtMs) < 0) return;
  nextPairAttemptAtMs = now + PAIR_RETRY_MS;

  const HueClient::Error error = client->pair();
  if (error == HueClient::Error::Ok) {
    if (!HOME_CONTROL_STORE.saveToFile()) LOG_ERR("HC", "Could not save Hue app key");
    loadRooms();
    return;
  }
  if (error == HueClient::Error::LinkButtonNotPressed) {
    if (static_cast<long>(now - pairingDeadlineMs) >= 0) {
      errorMessage = tr(STR_HUE_PAIRING_TIMEOUT);
      setState(State::ERROR);
      requestUpdate();
    }
    return;
  }
  failWith(error);
}

void HueRoomsActivity::failWith(const HueClient::Error error) {
  LOG_ERR("HC", "Hue screen error: %s", HueClient::errorName(error));
  errorMessage = messageFor(error);
  setState(State::ERROR);
  requestUpdate();
}

// One GET for every grouped_light: fills the room states and finds the
// bridge_home group used by All on / All off. The scratch list lives on this
// frame only while the response is parsed.
bool HueRoomsActivity::loadRoomStates() {
  auto groups = makeUniqueNoThrow<hue::GroupedLight[]>(hue::kMaxGroupedLights);
  if (!groups) {
    LOG_ERR("HC", "OOM: Hue grouped light scratch list");
    return false;
  }
  size_t groupCount = 0;
  const HueClient::Error error = client->listGroupedLights(groups.get(), hue::kMaxGroupedLights, groupCount);
  if (error != HueClient::Error::Ok) return false;

  allLightsId[0] = '\0';
  for (size_t i = 0; i < groupCount; ++i) {
    if (groups[i].ownedByBridgeHome) {
      std::snprintf(allLightsId, sizeof(allLightsId), "%s", groups[i].id);
      break;
    }
  }
  for (size_t i = 0; i < roomCount; ++i) {
    const hue::GroupedLight* group = hue::findGroupedLight(groups.get(), groupCount, rooms[i].groupedLightId);
    roomStateKnown[i] = group != nullptr;
    roomStates[i] = group != nullptr ? group->state : hue::RoomState{};
  }
  if (allLightsId[0] == '\0') LOG_INF("HC", "No bridge_home group; All on/off will loop the rooms");
  return true;
}

void HueRoomsActivity::loadRooms() {
  setState(State::LOADING_ROOMS);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  size_t count = 0;
  const HueClient::Error error = client->listRooms(rooms, hue::kMaxRooms, count);
  if (error == HueClient::Error::Unauthorized) {
    // The stored key no longer works (bridge reset or user removed the app).
    HOME_CONTROL_STORE.clearHuePairing();
    if (!HOME_CONTROL_STORE.saveToFile()) LOG_ERR("HC", "Could not clear Hue app key");
    startPairing();
    return;
  }
  if (error != HueClient::Error::Ok) {
    failWith(error);
    return;
  }
  roomCount = count;
  if (!loadRoomStates()) {
    // Tiles still show names; states stay unknown until a later refresh.
    for (size_t i = 0; i < roomCount; ++i) roomStateKnown[i] = false;
  }
  loadRoomScenes();
  {
    RenderLock lock(*this);
    selectedRoom = 0;
    page = 0;
    state = State::ROOMS;
  }
  requestUpdate();
}

// Scenes are an enhancement: any failure (typically a scene list larger than
// the buffer) leaves every sceneId empty and "on" falls back to a plain PUT.
void HueRoomsActivity::loadRoomScenes() {
  const HueClient::Error error = client->listLastScenes(rooms, roomCount, roomScenes);
  if (error != HueClient::Error::Ok) {
    LOG_ERR("HC", "Hue scenes unavailable (%s); Turn on will not recall scenes", HueClient::errorName(error));
    for (size_t i = 0; i < roomCount; ++i) roomScenes[i].sceneId[0] = '\0';
  }
}

// Turning a room on replays its last scene when it has one, as the Hue app
// does, then re-reads the group so the tile shows the scene's brightness.
HueClient::Error HueRoomsActivity::turnRoomOn(const int index) {
  const char* rid = rooms[index].groupedLightId;
  if (roomScenes[index].sceneId[0] == '\0') return client->setOn(rid, true);
  const HueClient::Error error = client->recallScene(roomScenes[index].sceneId);
  if (error != HueClient::Error::Ok) return error;
  roomScenes[index].active = true;
  hue::RoomState fresh;
  if (client->getRoomState(rid, fresh) == HueClient::Error::Ok) {
    RenderLock lock(*this);
    roomStates[index] = fresh;
    roomStateKnown[index] = true;
  }
  return HueClient::Error::Ok;
}

void HueRoomsActivity::openRoom(const int index) {
  if (index < 0 || index >= static_cast<int>(roomCount)) return;
  if (rooms[index].groupedLightId[0] == '\0') return;  // nothing controllable in this room
  {
    RenderLock lock(*this);
    selectedRoom = index;
    state = State::ROOM_DETAIL;
  }
  requestUpdate();
}

void HueRoomsActivity::applyDetailAction(const DetailAction action) {
  const char* rid = rooms[selectedRoom].groupedLightId;
  const hue::RoomState& current = roomStates[selectedRoom];
  // The PUT reply only lists the touched resource id, and a read-back costs a
  // second TLS round trip. A 2xx means the bridge accepted exactly what we
  // sent, so update the local state from the request instead.
  hue::RoomState next = current;
  HueClient::Error error = HueClient::Error::Ok;
  switch (action) {
    case DetailAction::TOGGLE:
      next.on = !current.on;
      if (next.on) {
        error = turnRoomOn(selectedRoom);
        if (error == HueClient::Error::Ok) next = roomStates[selectedRoom];  // read back after a scene recall
      } else {
        error = client->setOn(rid, false);
        roomScenes[selectedRoom].active = false;
      }
      break;
    case DetailAction::BRIGHTER:
      next.on = true;
      next.brightness = hue::clampBrightness(current.brightness + hue::kBrightnessStep);
      error = client->setBrightness(rid, next.brightness, !current.on);
      break;
    case DetailAction::DIMMER:
      next.brightness = hue::clampBrightness(current.brightness - hue::kBrightnessStep);
      error = client->setBrightness(rid, next.brightness, false);
      break;
  }
  if (error != HueClient::Error::Ok) {
    failWith(error);
    return;
  }
  {
    RenderLock lock(*this);
    roomStates[selectedRoom] = next;
    roomStateKnown[selectedRoom] = true;
  }
  requestUpdate();
}

void HueRoomsActivity::applyBulkAction(const BulkAction action) {
  const bool on = action == BulkAction::ALL_ON;
  setState(State::APPLYING);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);

  bool anyScene = false;
  for (size_t i = 0; i < roomCount; ++i) anyScene = anyScene || roomScenes[i].sceneId[0] != '\0';

  HueClient::Error error = HueClient::Error::Ok;
  if (on && anyScene) {
    // Rooms come back to their last scene; rooms without one just switch on.
    for (size_t i = 0; i < roomCount && error == HueClient::Error::Ok; ++i) {
      if (rooms[i].groupedLightId[0] == '\0') continue;
      error = roomScenes[i].sceneId[0] != '\0' ? client->recallScene(roomScenes[i].sceneId)
                                               : client->setOn(rooms[i].groupedLightId, true);
    }
    if (error == HueClient::Error::Ok) loadRoomStates();  // one bulk read for the scene brightnesses
  } else if (allLightsId[0] != '\0') {
    error = client->setOn(allLightsId, on);
  } else {
    for (size_t i = 0; i < roomCount && error == HueClient::Error::Ok; ++i) {
      if (rooms[i].groupedLightId[0] == '\0') continue;
      error = client->setOn(rooms[i].groupedLightId, on);
    }
  }
  if (error != HueClient::Error::Ok) {
    failWith(error);
    return;
  }
  {
    RenderLock lock(*this);
    for (size_t i = 0; i < roomCount; ++i) {
      if (rooms[i].groupedLightId[0] == '\0') continue;
      roomStates[i].on = on;
      roomStateKnown[i] = true;
      roomScenes[i].active = on && roomScenes[i].sceneId[0] != '\0';
    }
    state = State::ROOMS;
  }
  requestUpdate();
}

void HueRoomsActivity::handleBack() {
  if (state == State::ROOM_DETAIL) {
    {
      RenderLock lock(*this);
      state = State::ROOMS;
    }
    mappedInput.suppressNextBackRelease();
    requestUpdate();
    return;
  }
  finishAfterBackPress();
}

void HueRoomsActivity::handleErrorInput() {
  const auto actions = touchActionLayout(renderer, 2);
  bool consumed = false;
  const int tapped = tappedAction(mappedInput, actions, consumed);
  const bool retry = tapped == 0 || mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool changeBridge = tapped == 1;
  if (retry) {
    mappedInput.suppressNextConfirmRelease();
    if (!HOME_CONTROL_STORE.hasHuePairing()) {
      startPairing();
    } else {
      loadRooms();
    }
  } else if (changeBridge) {
    askForBridgeIp();
  }
}

void HueRoomsActivity::handleDetailInput() {
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
    applyDetailAction(DetailAction::TOGGLE);
    return;
  }
  buttonNavigator.onNext([this] { applyDetailAction(DetailAction::DIMMER); });
  buttonNavigator.onPrevious([this] { applyDetailAction(DetailAction::BRIGHTER); });
}

void HueRoomsActivity::handleRoomsInput() {
  const int count = static_cast<int>(roomCount);
  const auto geometry = HomeControlTileScreen::layout(renderer, mappedInput, count, page, kBulkActionCount);
  const auto event = HomeControlTileScreen::pollInput(mappedInput, buttonNavigator, geometry, count, selectedRoom);
  switch (event.kind) {
    case HomeControlTileScreen::EventKind::OpenTile:
      openRoom(event.value);
      break;
    case HomeControlTileScreen::EventKind::Action:
      if (event.value >= 0 && event.value < kBulkActionCount) applyBulkAction(static_cast<BulkAction>(event.value));
      break;
    case HomeControlTileScreen::EventKind::Select: {
      RenderLock lock(*this);
      selectedRoom = event.value;
      page = geometry.grid.pageSize > 0 ? selectedRoom / geometry.grid.pageSize : 0;
      requestUpdate();
      break;
    }
    case HomeControlTileScreen::EventKind::PageDelta: {
      const int next = std::max(0, std::min(page + event.value, geometry.grid.pageCount - 1));
      if (next == page) break;
      RenderLock lock(*this);
      page = next;
      selectedRoom = std::min(count - 1, page * geometry.grid.pageSize);
      requestUpdate();
      break;
    }
    case HomeControlTileScreen::EventKind::None:
      break;
  }
}

void HueRoomsActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    handleBack();
    return;
  }
  switch (state) {
    case State::PAIRING:
      pairingTick();
      break;
    case State::ROOMS:
      handleRoomsInput();
      break;
    case State::ROOM_DETAIL:
      handleDetailInput();
      break;
    case State::ERROR:
      handleErrorInput();
      break;
    case State::SEARCHING:
    case State::NEED_IP:
    case State::LOADING_ROOMS:
    case State::APPLYING:
      break;
  }
}

void HueRoomsActivity::formatRoomLine(const int index, char* out, const size_t cap) const {
  if (rooms[index].groupedLightId[0] == '\0' || !roomStateKnown[index]) {
    out[0] = '\0';
    return;
  }
  const hue::RoomState& s = roomStates[index];
  if (s.on) {
    std::snprintf(out, cap, "%s · %u%%", tr(STR_HUE_ROOM_ON), s.brightness);
  } else {
    std::snprintf(out, cap, "%s", tr(STR_HUE_ROOM_OFF));
  }
}

void HueRoomsActivity::renderMessage(const char* title, const char* body, const bool showRetry) {
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
    const char* labels[] = {tr(STR_RETRY), tr(STR_HUE_CHANGE_BRIDGE)};
    TouchActionButtons::draw(renderer, actions, labels, 0, -1, UI_10_FONT_ID);
  }
}

void HueRoomsActivity::renderRooms() {
  const int count = static_cast<int>(roomCount);
  const auto geometry = HomeControlTileScreen::layout(renderer, mappedInput, count, page, kBulkActionCount);
  const bool showSelection = !mappedInput.hasTouch();
  char line[40];
  for (int i = 0; i < geometry.grid.count; ++i) {
    const int index = geometry.grid.firstIndex + i;
    formatRoomLine(index, line, sizeof(line));
    TileGrid::Content content;
    content.title = rooms[index].name;
    content.line1 = line;
    const hue::RoomScene& scene = roomScenes[index];
    content.line2 = (scene.active && roomStateKnown[index] && roomStates[index].on) ? scene.name : nullptr;
    content.icon = &icon_bulb_32;
    content.enabled = rooms[index].groupedLightId[0] != '\0';
    content.filled = roomStateKnown[index] && roomStates[index].on;
    TileGrid::drawTile(renderer, geometry.grid.tiles[i], content, showSelection && index == selectedRoom, UI_10_FONT_ID,
                       UI_10_FONT_ID);
  }
  const char* labels[] = {tr(STR_HUE_ALL_ON), tr(STR_HUE_ALL_OFF)};
  HomeControlTileScreen::drawChrome(renderer, geometry, labels, mappedInput.hasTouch());
}

void HueRoomsActivity::renderDetail() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const hue::RoomState& s = roomStates[selectedRoom];
  int y = header.y + header.height + metrics.verticalSpacing * 2;

  renderer.drawCenteredText(UI_12_FONT_ID, y, s.on ? tr(STR_HUE_ROOM_ON) : tr(STR_HUE_ROOM_OFF), true,
                            EpdFontFamily::BOLD);
  y += lineHeight + metrics.verticalSpacing;

  char line[64];
  std::snprintf(line, sizeof(line), "%s: %u%%", tr(STR_HUE_BRIGHTNESS), s.brightness);
  renderer.drawCenteredText(UI_10_FONT_ID, y, line);
  const hue::RoomScene& scene = roomScenes[selectedRoom];
  if (scene.sceneId[0] != '\0') {
    y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    std::snprintf(line, sizeof(line), "%s: %s", tr(STR_HUE_SCENE), scene.name);
    renderer.drawCenteredText(UI_10_FONT_ID, y, line);
  }

  if (mappedInput.hasTouch()) {
    const auto actions = touchActionLayout(renderer, kDetailActionCount);
    const char* labels[] = {s.on ? tr(STR_HUE_TURN_OFF) : tr(STR_HUE_TURN_ON), tr(STR_HUE_BRIGHTER),
                            tr(STR_HUE_DIMMER)};
    TouchActionButtons::draw(renderer, actions, labels, 0, -1, UI_10_FONT_ID);
  }
}

void HueRoomsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const char* title = state == State::ROOM_DETAIL ? rooms[selectedRoom].name : tr(STR_HUE);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, title, false);
  } else {
    GUI.drawHeader(renderer, header, title);
  }

  const char* hint2 = "";
  const char* hint3 = "";
  const char* hint4 = "";
  switch (state) {
    case State::ROOMS:
      if (roomCount == 0) {
        renderMessage(tr(STR_HUE_ROOMS), tr(STR_HUE_NO_ROOMS), false);
      } else {
        renderRooms();
        hint2 = tr(STR_SELECT);
        hint3 = tr(STR_DIR_UP);
        hint4 = tr(STR_DIR_DOWN);
      }
      break;
    case State::ROOM_DETAIL:
      renderDetail();
      hint2 = roomStates[selectedRoom].on ? tr(STR_HUE_TURN_OFF) : tr(STR_HUE_TURN_ON);
      hint3 = tr(STR_HUE_BRIGHTER);
      hint4 = tr(STR_HUE_DIMMER);
      break;
    case State::PAIRING:
      renderMessage(tr(STR_HUE_PAIRING_TITLE), tr(STR_HUE_PRESS_LINK_BUTTON), false);
      break;
    case State::LOADING_ROOMS:
      renderMessage(tr(STR_HOME_CONTROL_LOADING_ROOMS), nullptr, false);
      break;
    case State::APPLYING:
      renderMessage(tr(STR_HOME_CONTROL_APPLYING), nullptr, false);
      break;
    case State::ERROR:
      renderMessage(tr(STR_HUE), errorMessage, true);
      hint2 = tr(STR_RETRY);
      break;
    case State::SEARCHING:
      renderMessage(tr(STR_HUE_SEARCHING), nullptr, false);
      break;
    case State::NEED_IP:
      break;
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), hint2, hint3, hint4);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(screenTransitionRefresh.modeFor(static_cast<uint8_t>(state)));
}

#endif  // CROSSINK_APP_CAP_HOME_CONTROL
