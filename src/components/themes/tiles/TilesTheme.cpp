#include "TilesTheme.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>
#include <vector>

#include "components/TouchRegistry.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace {
constexpr uint32_t kIconSize = 32;
constexpr int kLabelSidePadding = 8;
constexpr int kLabelMaxLines = 2;

// Multi-word labels split into two lines even when they would fit on one, so
// "File Transfer" and "Browse Files" read the same way side by side. Falls
// back to width-based wrapping when a half would not fit.
std::vector<std::string> tileLabelLines(const GfxRenderer& renderer, const char* label, const int maxWidth) {
  const std::string text(label);
  const size_t split = text.find_last_of(' ');
  if (split != std::string::npos && split > 0 && split + 1 < text.size()) {
    std::vector<std::string> lines{text.substr(0, split), text.substr(split + 1)};
    if (renderer.getTextWidth(UI_10_FONT_ID, lines[0].c_str()) <= maxWidth &&
        renderer.getTextWidth(UI_10_FONT_ID, lines[1].c_str()) <= maxWidth) {
      return lines;
    }
  }
  return renderer.wrappedText(UI_10_FONT_ID, label, maxWidth, kLabelMaxLines);
}

// The bookmark entry has no Lucide bitmap; Lyra draws the same ribbon shape.
void drawBookmarkRibbon(const GfxRenderer& renderer, const int centerX, const int top) {
  constexpr int ribbonWidth = 16;
  constexpr int ribbonHeight = 22;
  constexpr int notchSize = 6;
  const int iconX = centerX - ribbonWidth / 2;
  const int polyX[5] = {iconX, iconX + ribbonWidth, iconX + ribbonWidth, centerX, iconX};
  const int polyY[5] = {top, top, top + ribbonHeight, top + ribbonHeight - notchSize, top + ribbonHeight};
  renderer.fillPolygon(polyX, polyY, 5, true);
}
}  // namespace

void TilesTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                const std::function<const char*(int index)>& buttonLabel,
                                const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();

  // HomeActivity passes a nominal height; clamp to what is really left above the hints.
  const int usableHeight =
      std::max(1, std::min(rect.height, renderer.getScreenHeight() - rect.y - metrics.buttonHintsHeight) -
                      metrics.verticalSpacing);
  const int gridWidth = rect.width - metrics.contentSidePadding * 2;
  const int tileWidth = std::max(1, (gridWidth - kTileGap * (kColumns - 1)) / kColumns);
  const int rows = std::clamp((usableHeight + kTileGap) / (tileWidth + kTileGap), 1, kMaxRows);
  const int tileHeight = std::min(tileWidth, (usableHeight + kTileGap) / rows - kTileGap);
  const int pageItems = rows * kColumns;
  const int totalPages = (buttonCount + pageItems - 1) / pageItems;
  const int page = selectedIndex >= 0 ? std::min(selectedIndex / pageItems, totalPages - 1) : 0;
  const int pageStart = page * pageItems;

  const int labelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  // Icon above up to two label lines, the block centered vertically in the tile.
  const int contentHeight = static_cast<int>(kIconSize) + 6 + labelHeight * kLabelMaxLines;
  const int gridX = rect.x + metrics.contentSidePadding;

  for (int i = pageStart; i < buttonCount && i < pageStart + pageItems; ++i) {
    const int slot = i - pageStart;
    const int col = slot % kColumns;
    const int row = slot / kColumns;
    const Rect tile{gridX + col * (tileWidth + kTileGap), rect.y + row * (tileHeight + kTileGap), tileWidth,
                    tileHeight};
    TouchRegistry::getInstance().add(tile, i, TouchRegistry::Item);

    const bool selected = selectedIndex == i;
    if (selected) {
      renderer.fillRoundedRect(tile.x, tile.y, tile.width, tile.height, kCornerRadius, Color::LightGray);
    }
    renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, selected ? 3 : 2, kCornerRadius, true);

    const int centerX = tile.x + tile.width / 2;
    int y = tile.y + std::max(0, (tile.height - contentHeight) / 2);
    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      const freeink::Icon* bitmap = iconForName(icon, kIconSize);
      if (bitmap != nullptr) {
        drawLucideIcon(renderer, *bitmap, centerX - static_cast<int>(kIconSize) / 2, y);
      } else if (icon == UIIcon::BookmarkIcon) {
        drawBookmarkRibbon(renderer, centerX, y + 5);
      }
    }
    y += static_cast<int>(kIconSize) + 6;

    const char* label = buttonLabel != nullptr ? buttonLabel(i) : "";
    if (label == nullptr) label = "";
    const int maxLabelWidth = tile.width - kLabelSidePadding * 2;
    // Home redraws rarely, so the per-tile wrap allocation is not a hot path.
    const std::vector<std::string> lines = tileLabelLines(renderer, label, maxLabelWidth);
    for (const std::string& line : lines) {
      const int lineWidth = renderer.getTextWidth(UI_10_FONT_ID, line.c_str());
      renderer.drawText(UI_10_FONT_ID, centerX - lineWidth / 2, y, line.c_str(), true);
      y += labelHeight;
    }
  }

  if (totalPages > 1) {
    constexpr int dot = 8;
    constexpr int step = 16;
    const int dotsY = rect.y + rows * (tileHeight + kTileGap) + 2;
    int x = rect.x + (rect.width - (dot + step * (totalPages - 1))) / 2;
    for (int p = 0; p < totalPages; ++p, x += step) {
      if (p == page) {
        renderer.fillRoundedRect(x, dotsY, dot, dot, dot / 2, Color::Black);
      } else {
        renderer.drawRoundedRect(x, dotsY, dot, dot, 1, dot / 2, true);
      }
    }
  }
}
