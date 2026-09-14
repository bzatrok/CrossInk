#include "HomeControlActivity.h"

#if CROSSINK_APP_CAP_HOME_CONTROL

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "HueRoomsActivity.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "TadoZonesActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/homeControlIcons.h"
#include "fontIds.h"
#include "network/WifiUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}

HomeControlActivity::HomeControlActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("HomeControl", renderer, mappedInput), ui(renderer) {}

void HomeControlActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseLoadedFont(renderer);

  selectedIndex = 0;
  ui.closeRouting();
  ui.reset();
  ui.app.on(ACTION_ROW, &HomeControlActivity::onRowEvent, this);
  ui.app.setScreen(&HomeControlActivity::listScreen, this);

#ifdef SIMULATOR
  onWifiSelectionComplete(true);  // fixtures answer every request; no radio to bring up
#else
  if (hasActiveStationWifiConnection()) {
    onWifiSelectionComplete(true);
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
#endif
}

void HomeControlActivity::onExit() {
  Activity::onExit();
#ifndef SIMULATOR
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
  // Launched from the minimal network boot, so restore the full app even if
  // Wi-Fi never came up.
  silentRestartAfterNetwork();
#endif
}

void HomeControlActivity::onWifiSelectionComplete(const bool success) {
  {
    RenderLock lock(*this);
    if (!success) {
      state = State::FAILED;
    } else {
#ifndef SIMULATOR
      WiFi.setSleep(false);  // keep the radio responsive between requests
      sdFontSystem.releaseForNetwork(renderer);
#endif
      state = State::MENU;
      LOG_INF("HC", "Home Control ready (free=%u maxAlloc=%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }
  }
  requestUpdate();
}

void HomeControlActivity::openService(const Service service) {
  ui.closeRouting();
  std::unique_ptr<Activity> child;
  if (service == Service::HUE) {
    child = std::make_unique<HueRoomsActivity>(renderer, mappedInput);
  } else {
    child = std::make_unique<TadoZonesActivity>(renderer, mappedInput);
  }
  startActivityForResult(std::move(child), [this](const ActivityResult&) {
    mappedInput.suppressNextConfirmRelease();
    ui.reset();
    requestUpdate();
  });
}

void HomeControlActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<HomeControlActivity*>(user);
  if (event.value < 0 || event.value >= kServiceCount) return;
  self->selectedIndex = event.value;
  self->ui.app.clearTapFlash();
  self->openService(static_cast<Service>(event.value));
}

void HomeControlActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishAfterBackPress();
    return;
  }
  if (state != State::MENU) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    mappedInput.suppressNextConfirmRelease();
    openService(static_cast<Service>(selectedIndex));
    return;
  }

  if (ui.routingReady()) {
    fui::ActionEvent event{};
    if (ui.routeTouch(mappedInput, event)) {
      if (ui.app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  buttonNavigator.onNext([this] {
    {
      RenderLock lock(*this);
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, kServiceCount);
    }
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    {
      RenderLock lock(*this);
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, kServiceCount);
    }
    requestUpdate();
  });
}

void HomeControlActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<HomeControlActivity*>(user)->buildListScreen(screen);
}

void HomeControlActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput)), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Two fixed rows; storage is static so the ListProps pointer stays valid
  // across the FreeInkApp render passes.
  static fui::ListItem items[kServiceCount];
  items[0] = fui::ListItem{};
  items[0].label = tr(STR_HUE);
  items[0].subtitle = tr(STR_HUE_DESC);
  items[0].icon = fui::bitmapFromIcon(icon_bulb_32);
  items[0].actionValue = static_cast<int16_t>(Service::HUE);
  items[1] = fui::ListItem{};
  items[1].label = tr(STR_TADO);
  items[1].subtitle = tr(STR_TADO_DESC);
  items[1].icon = fui::bitmapFromIcon(icon_temperature_32);
  items[1].actionValue = static_cast<int16_t>(Service::TADO);

  fui::ListProps props;
  props.items = items;
  props.count = kServiceCount;
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.labelText = screen.theme().bodyText;
  props.labelText.bold = true;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.bold = false;
  props.subtitleText.maxLines = 2;
  props.rowGap = 10;
  configureUiList(props, screen.theme(), screen.body(), UiListRowType::WithSubtitle);
  screen.list(props);
}

void HomeControlActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, ui.target, header, tr(STR_HOME_CONTROL), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_HOME_CONTROL));
  }

  if (state == State::MENU) {
    ui.closeRouting();
    ui.render();
  } else {
    const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top = (renderer.getScreenHeight() - lineHeight) / 2;
    if (state == State::CONNECTING) {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CONNECTING));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_WIFI_CONN_FAILED), true, EpdFontFamily::BOLD);
    }
    (void)metrics;
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)),
                                            state == State::MENU ? tr(STR_SELECT) : "", tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(screenTransitionRefresh.modeFor(static_cast<uint8_t>(state)));
}

#endif  // CROSSINK_APP_CAP_HOME_CONTROL
