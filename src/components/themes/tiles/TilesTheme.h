#pragma once

#include <cstdint>

#include "components/themes/lyra/LyraTheme.h"

// Tiles theme metrics: Lyra everywhere except the home menu, which becomes a
// phone-style icon grid below the recent-book cover.
namespace TilesMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics v = LyraMetrics::values;
  v.homeMenuTopOffset = 8;
  return v;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace TilesMetrics

class TilesTheme : public LyraTheme {
 public:
  static constexpr int kColumns = 3;
  static constexpr int kMaxRows = 4;
  static constexpr int kTileGap = 12;
  static constexpr int kCornerRadius = 18;

  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<const char*(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
