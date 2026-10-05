#include "DashboardRender.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "DashboardClock.h"
#include "DashboardImageStore.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "fontIds.h"

namespace DashboardRender {

namespace {
// Full-width black strip at the bottom of the oriented viewable area, white text.
void drawBanner(GfxRenderer& renderer, const char* text) {
  int top = 0, right = 0, bottom = 0, left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int stripY = height - bottom - lineHeight;
  renderer.fillRect(0, stripY, width, lineHeight + bottom, true);
  const std::string visible = renderer.truncatedText(UI_10_FONT_ID, text, width - left - right);
  const int textX = left + (width - left - right - renderer.getTextWidth(UI_10_FONT_ID, visible.c_str())) / 2;
  renderer.drawText(UI_10_FONT_ID, textX, stripY, visible.c_str(), false);
}
}  // namespace

HalDisplay::RefreshMode chooseRefreshMode(dashboard::DashboardState& state, const bool frameRestored) {
  if (frameRestored && !state.showsFallback() && state.fastRefreshesSinceHalf < MAX_FAST_REFRESHES) {
    ++state.fastRefreshesSinceHalf;
    return HalDisplay::FAST_REFRESH;
  }
  state.fastRefreshesSinceHalf = 0;
  return HalDisplay::HALF_REFRESH;
}

bool draw(GfxRenderer& renderer, const char* bannerText, const HalDisplay::RefreshMode mode) {
  if (!DashboardImageStore::hasCurrent()) return false;
  FsFile file;
  if (!Storage.openFileForRead("DSH", DashboardImageStore::CURRENT_BMP, file)) return false;
  Bitmap bitmap(file);
  const BmpReaderError err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_ERR("DSH", "current.bmp does not parse: %s", Bitmap::errorToString(err));
    file.close();
    return false;
  }

  // TRMNL images are 800x480 landscape; draw them full-screen in landscape.
  const GfxRenderer::Orientation previous = renderer.getOrientation();
  if (bitmap.getWidth() > bitmap.getHeight()) {
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  }
  const bool drawn = SleepActivity::drawBitmapToFramebuffer(renderer, bitmap);
  file.close();
  if (drawn && bannerText) drawBanner(renderer, bannerText);
  renderer.setOrientation(previous);
  if (!drawn) {
    LOG_ERR("DSH", "Could not draw current.bmp");
    return false;
  }

  renderer.displayBuffer(mode, true);
  DashboardImageStore::saveFrame(renderer.getFrameBuffer(), renderer.getBufferSize());
  return true;
}

void formatBanner(const dashboard::DashboardState& state, char* buf, const size_t bufSize) {
  if (state.lastSuccessUtc == 0) {
    snprintf(buf, bufSize, "%s", tr(STR_DASHBOARD_NOT_UPDATED_YET));
    return;
  }
  char time[8];
  dashboard::clock::formatLocalTime(state.lastSuccessUtc, time, sizeof(time));
  snprintf(buf, bufSize, tr(STR_DASHBOARD_STALE), time);
}

}  // namespace DashboardRender

#endif  // CROSSINK_APP_CAP_DASHBOARD
