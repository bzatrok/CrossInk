#pragma once

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

/**
 * Home Control hub (x4-pro only): connects Wi-Fi once, then offers the
 * Philips Hue and tado° screens as child activities. Launched from the home
 * menu through a minimal network boot; leaving it restarts back to Home so
 * the Wi-Fi heap is released, the same way KOReader auth and OPDS work.
 */
class HomeControlActivity final : public Activity {
 public:
  explicit HomeControlActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::CONNECTING; }

 private:
  enum class State : uint8_t { CONNECTING, MENU, FAILED };
  enum class Service : uint8_t { HUE = 0, TADO = 1 };
  static constexpr int kServiceCount = 2;

  using UiHost = UiAppHost<6, 2>;
  using UiApp = UiHost::App;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  void onWifiSelectionComplete(bool success);
  void openService(Service service);

  State state = State::CONNECTING;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
  UiHost ui;
  ScreenTransitionRefresh screenTransitionRefresh;
};
