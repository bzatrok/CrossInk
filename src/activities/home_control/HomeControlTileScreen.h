#pragma once

#include <cstdint>

#include "components/TileGrid.h"
#include "components/TouchActionButtons.h"

class GfxRenderer;
class MappedInputManager;
class ButtonNavigator;

// Geometry and input shared by the Hue rooms and tado zones screens: a paged
// tile grid under the header, a page indicator, and one row of bulk-action
// buttons above the button hints. Stateless; the activities own selection and
// page and apply the returned events under their render lock.
namespace HomeControlTileScreen {

constexpr int kMaxActions = 2;

struct Geometry {
  TileGrid::Layout grid;
  Rect indicator;
  TouchActionButtons::Layout actions;
};

Geometry layout(const GfxRenderer& renderer, const MappedInputManager& mappedInput, int itemCount, int page,
                int actionCount);

enum class EventKind : uint8_t { None, OpenTile, Action, Select, PageDelta };
struct Event {
  EventKind kind = EventKind::None;
  int value = 0;  // OpenTile/Select: item index; Action: button index; PageDelta: +1/-1
};

// Reads touch and buttons for one loop tick. Confirm opens the selected tile,
// Up/Down move the selection, vertical swipes page.
Event pollInput(MappedInputManager& mappedInput, ButtonNavigator& navigator, const Geometry& geometry, int itemCount,
                int selected);

void drawChrome(const GfxRenderer& renderer, const Geometry& geometry, const char* const* actionLabels,
                bool showActions);

}  // namespace HomeControlTileScreen
