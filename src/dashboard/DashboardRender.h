#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <HalDisplay.h>

#include "DashboardState.h"

class GfxRenderer;

// Draws current.bmp (with an optional bottom banner), refreshes the panel and
// saves the framebuffer to frame.bin for the next wake's restore.
namespace DashboardRender {

// FAST (differential) refreshes in a row before a HALF refresh clears ghosting.
constexpr uint8_t MAX_FAST_REFRESHES = 30;

// FAST when the previous frame was restored into the panel on this wake, the
// glass already shows a dashboard frame (not a fallback or nothing), and fewer than MAX_FAST_REFRESHES ran
// since the last HALF. Otherwise HALF. Updates state.fastRefreshesSinceHalf.
HalDisplay::RefreshMode chooseRefreshMode(dashboard::DashboardState& state, bool frameRestored);

// Draws the image and the banner (nullptr = no banner), refreshes with `mode`,
// then writes frame.bin. False when current.bmp is missing or does not draw;
// the panel is then untouched.
bool draw(GfxRenderer& renderer, const char* bannerText, HalDisplay::RefreshMode mode);

// Banner text for the current state: "Not updated since HH:MM" or "Not updated yet".
void formatBanner(const dashboard::DashboardState& state, char* buf, size_t bufSize);

}  // namespace DashboardRender

#endif  // CROSSINK_APP_CAP_DASHBOARD
