#pragma once

#include <cstddef>
#include <cstdint>

#include "themes/BaseTheme.h"

class GfxRenderer;
namespace freeink {
struct Icon;
}

// Paged tile layout for touch screens. Drawing and hit testing share one
// Layout so the two paths cannot drift apart. Tiles are laid out row-major
// inside the container; items beyond one page are reached by paging.
namespace TileGrid {

constexpr int kMaxTilesPerPage = 12;
constexpr int kDefaultColumns = 2;
constexpr int kDefaultTileHeight = 132;
constexpr int kDefaultGap = 12;
constexpr int kCornerRadius = 14;
constexpr int kInnerPadding = 12;
constexpr int kIconSize = 32;

struct Layout {
  Rect container;
  Rect tiles[kMaxTilesPerPage];
  int columns = 0;
  int rows = 0;
  int pageSize = 0;   // tiles per full page
  int pageCount = 0;  // at least 1
  int page = 0;       // clamped
  int firstIndex = 0;
  int count = 0;  // tiles on this page

  Layout() = default;
};

// Lays out page `page` of `itemCount` items. Row count comes from the container height.
Layout paged(Rect container, int itemCount, int page, int columns = kDefaultColumns,
             int tileHeight = kDefaultTileHeight, int gap = kDefaultGap);

// Item index under (x, y) on this page, or -1.
int indexAt(const Layout& layout, int x, int y);

struct Content {
  const char* title = nullptr;
  const char* line1 = nullptr;
  const char* line2 = nullptr;
  const freeink::Icon* icon = nullptr;  // SDK icon (kIconSize square), or nullptr
  bool filled = false;                  // "active" look: black tile with white text
  bool enabled = true;                  // disabled tiles draw the title only
};

void drawTile(const GfxRenderer& renderer, const Rect& tile, const Content& content, bool selected, int titleFontId,
              int bodyFontId);

// Small centered dots, one per page, the current one filled.
void drawPageIndicator(const GfxRenderer& renderer, Rect area, int pageCount, int page);

}  // namespace TileGrid
