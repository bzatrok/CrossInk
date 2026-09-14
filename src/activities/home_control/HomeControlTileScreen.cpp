#include "HomeControlTileScreen.h"

#if CROSSINK_APP_CAP_HOME_CONTROL

#include <GfxRenderer.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

namespace HomeControlTileScreen {

namespace {
constexpr int kIndicatorHeight = 20;
}

Geometry layout(const GfxRenderer& renderer, const MappedInputManager& mappedInput, const int itemCount,
                const int page, const int actionCount) {
  Geometry g;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const int left = screen.x + metrics.contentSidePadding;
  const int width = std::max(1, screen.width - metrics.contentSidePadding * 2);
  const int bottom = screen.y + screen.height - metrics.verticalSpacing;

  const int actionsHeight = actionCount > 0 ? TouchActionButtons::kDefaultHeight : 0;
  g.actions = TouchActionButtons::horizontal(Rect{left, bottom - actionsHeight, width, actionsHeight},
                                             static_cast<uint8_t>(actionCount));
  const int indicatorBottom = actionCount > 0 ? g.actions.container.y - metrics.verticalSpacing : bottom;
  g.indicator = Rect{left, indicatorBottom - kIndicatorHeight, width, kIndicatorHeight};

  const int top = header.y + header.height + metrics.verticalSpacing * 2;
  const int gridHeight = std::max(1, g.indicator.y - metrics.verticalSpacing - top);
  g.grid = TileGrid::paged(Rect{left, top, width, gridHeight}, itemCount, page);
  return g;
}

Event pollInput(MappedInputManager& mappedInput, ButtonNavigator& navigator, const Geometry& geometry,
                const int itemCount, const int selected) {
  Event event;
  int x = 0;
  int y = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y)) {
    const int action = TouchActionButtons::indexAt(geometry.actions, x, y);
    if (action >= 0) {
      event.kind = EventKind::Action;
      event.value = action;
      return event;
    }
    const int tile = TileGrid::indexAt(geometry.grid, x, y);
    if (tile >= 0 && tile < itemCount) {
      event.kind = EventKind::OpenTile;
      event.value = tile;
      return event;
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    mappedInput.suppressNextConfirmRelease();
    if (selected >= 0 && selected < itemCount) {
      event.kind = EventKind::OpenTile;
      event.value = selected;
    }
    return event;
  }
  if (itemCount <= 0) return event;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    event.kind = EventKind::PageDelta;
    event.value = swipe == MappedInputManager::SwipeDir::Up ? 1 : -1;
    return event;
  }
  navigator.onNext([&] {
    event.kind = EventKind::Select;
    event.value = ButtonNavigator::nextIndex(selected, itemCount);
  });
  navigator.onPrevious([&] {
    event.kind = EventKind::Select;
    event.value = ButtonNavigator::previousIndex(selected, itemCount);
  });
  return event;
}

void drawChrome(const GfxRenderer& renderer, const Geometry& geometry, const char* const* actionLabels,
                const bool showActions) {
  TileGrid::drawPageIndicator(renderer, geometry.indicator, geometry.grid.pageCount, geometry.grid.page);
  if (showActions && geometry.actions.count > 0) {
    TouchActionButtons::draw(renderer, geometry.actions, actionLabels, -1, -1, UI_10_FONT_ID);
  }
}

}  // namespace HomeControlTileScreen

#endif  // CROSSINK_APP_CAP_HOME_CONTROL
