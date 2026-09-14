#include "TileGrid.h"

#include <FreeInkUIGfxRenderer.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <string>

namespace TileGrid {

Layout paged(const Rect container, const int itemCount, const int page, const int columns, const int tileHeight,
             const int gap) {
  Layout result;
  result.container = container;
  result.columns = std::max(1, std::min(columns, kMaxTilesPerPage));
  const int safeGap = std::max(0, gap);
  const int safeHeight = std::max(1, tileHeight);
  const int rowsThatFit = (container.height + safeGap) / (safeHeight + safeGap);
  result.rows = std::max(1, std::min(rowsThatFit, kMaxTilesPerPage / result.columns));
  result.pageSize = result.rows * result.columns;
  const int items = std::max(0, itemCount);
  result.pageCount = std::max(1, (items + result.pageSize - 1) / result.pageSize);
  result.page = std::max(0, std::min(page, result.pageCount - 1));
  result.firstIndex = result.page * result.pageSize;
  result.count = std::max(0, std::min(result.pageSize, items - result.firstIndex));

  const int tileWidth = std::max(1, (container.width - safeGap * (result.columns - 1)) / result.columns);
  for (int i = 0; i < result.count; ++i) {
    const int col = i % result.columns;
    const int row = i / result.columns;
    result.tiles[i] = Rect{container.x + col * (tileWidth + safeGap), container.y + row * (safeHeight + safeGap),
                           tileWidth, safeHeight};
  }
  return result;
}

int indexAt(const Layout& layout, const int x, const int y) {
  for (int i = 0; i < layout.count; ++i) {
    const Rect& tile = layout.tiles[i];
    if (x >= tile.x && x < tile.x + tile.width && y >= tile.y && y < tile.y + tile.height) {
      return layout.firstIndex + i;
    }
  }
  return -1;
}

namespace {

void drawLineClipped(const GfxRenderer& renderer, const int fontId, const int x, const int y, const int maxWidth,
                     const char* text, const bool black, const EpdFontFamily::Style style) {
  if (text == nullptr || text[0] == '\0') return;
  if (renderer.getTextWidth(fontId, text, style) <= maxWidth) {
    renderer.drawText(fontId, x, y, text, black, style);
    return;
  }
  // Rare path (long room names); the std::string lives only for this draw.
  const std::string shortened = renderer.truncatedText(fontId, text, maxWidth, style);
  renderer.drawText(fontId, x, y, shortened.c_str(), black, style);
}

}  // namespace

void drawTile(const GfxRenderer& renderer, const Rect& tile, const Content& content, const bool selected,
              const int titleFontId, const int bodyFontId) {
  const bool filled = content.filled && content.enabled;
  if (filled) {
    renderer.fillRoundedRect(tile.x, tile.y, tile.width, tile.height, kCornerRadius, Color::Black);
  } else {
    renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, content.enabled ? 2 : 1, kCornerRadius, true);
  }
  if (selected) {
    // A ring outside the tile reads on both filled and outlined tiles.
    renderer.drawRoundedRect(tile.x - 4, tile.y - 4, tile.width + 8, tile.height + 8, 2, kCornerRadius + 4, true);
  }
  const bool black = !filled;
  const int innerX = tile.x + kInnerPadding;
  const int innerWidth = tile.width - kInnerPadding * 2;
  int y = tile.y + kInnerPadding;
  const int bottom = tile.y + tile.height - kInnerPadding / 2;

  int textX = innerX;
  if (content.icon != nullptr) {
    // SDK icons go through the FreeInkUI adapter so they stay upright in every
    // orientation (GfxRenderer::drawIcon expects pre-rotated legacy arrays).
    freeink::ui::GfxRendererTarget target(renderer);
    target.bitmap(freeink::ui::Rect{static_cast<int16_t>(innerX), static_cast<int16_t>(y),
                                    static_cast<int16_t>(content.icon->w), static_cast<int16_t>(content.icon->h)},
                  freeink::ui::bitmapFromIcon(*content.icon), freeink::ui::BitmapMode::Center,
                  freeink::ui::Paint::solid(filled ? freeink::ui::Color::White : freeink::ui::Color::Black));
    textX = innerX + kIconSize + 8;
  }
  const int titleHeight = renderer.getLineHeight(titleFontId);
  const int titleY = y + std::max(0, (kIconSize - titleHeight) / 2);
  drawLineClipped(renderer, titleFontId, textX, titleY, innerWidth - (textX - innerX), content.title, black,
                  EpdFontFamily::BOLD);
  y += std::max(kIconSize, titleHeight) + 8;
  if (!content.enabled) return;

  const int bodyHeight = renderer.getLineHeight(bodyFontId);
  if (content.line1 != nullptr && y + bodyHeight <= bottom) {
    drawLineClipped(renderer, bodyFontId, innerX, y, innerWidth, content.line1, black, EpdFontFamily::REGULAR);
    y += bodyHeight;
  }
  if (content.line2 != nullptr && y + bodyHeight <= bottom) {
    drawLineClipped(renderer, bodyFontId, innerX, y, innerWidth, content.line2, black, EpdFontFamily::REGULAR);
  }
}

void drawPageIndicator(const GfxRenderer& renderer, const Rect area, const int pageCount, const int page) {
  if (pageCount <= 1) return;
  constexpr int dot = 8;
  constexpr int step = 16;
  const int totalWidth = dot + step * (pageCount - 1);
  int x = area.x + (area.width - totalWidth) / 2;
  const int y = area.y + (area.height - dot) / 2;
  for (int i = 0; i < pageCount; ++i, x += step) {
    if (i == page) {
      renderer.fillRoundedRect(x, y, dot, dot, dot / 2, Color::Black);
    } else {
      renderer.drawRoundedRect(x, y, dot, dot, 1, dot / 2, true);
    }
  }
}

}  // namespace TileGrid
