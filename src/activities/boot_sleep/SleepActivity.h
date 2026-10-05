#pragma once
#include <string>
#include <utility>

#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool canSnapshotOverlayBackground,
                         std::string currentBookPath = {}, bool fromTimeout = false,
                         GfxRenderer::Orientation sleepPopupOrientation = GfxRenderer::Orientation::Portrait)
      : Activity("Sleep", renderer, mappedInput),
        canSnapshotOverlayBackground(canSnapshotOverlayBackground),
        currentBookPath(std::move(currentBookPath)),
        fromTimeout(fromTimeout),
        sleepPopupOrientation(sleepPopupOrientation) {}
  void onEnter() override;
  // Whether this screen is independent of the outgoing activity's saves.
  bool rendersBeforeExit() const;

  // Where drawBitmapToFramebuffer() placed the image, for later grayscale passes.
  struct BitmapPlacement {
    int x = 0;
    int y = 0;
    float cropX = 0;
    float cropY = 0;
  };
  // Places `bitmap` per the sleep cover mode, clears the framebuffer and, when
  // drawBlackWhite, draws the B/W image and applies the inverted filter. Never
  // refreshes the panel; the caller picks the refresh mode.
  static bool drawBitmapToFramebuffer(GfxRenderer& renderer, Bitmap& bitmap, BitmapPlacement* placementOut = nullptr,
                                      bool drawBlackWhite = true);

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderMinimalSleepScreen() const;
  void renderMinimalStatsSleepScreen() const;
  void renderDashboardSleepScreen() const;
  bool renderBitmapSleepScreen(Bitmap& bitmap) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  void renderOverlaySleepScreen() const;
  bool canSnapshotOverlayBackground = false;
  bool overlayBackgroundBufferStored = false;
  std::string currentBookPath;
  bool fromTimeout = false;
  GfxRenderer::Orientation sleepPopupOrientation = GfxRenderer::Orientation::Portrait;
};
