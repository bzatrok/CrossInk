#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include "DashboardSource.h"

// Dashboard source for a self-hosted TRMNL BYOS server (Terminus, byos_next).
// Provisions through /api/setup when no API key is set, polls /api/display,
// and downloads the image. A PNG is converted to a 1-bit 800x480 BMP.
class TrmnlSource final : public DashboardSource {
 public:
  static constexpr const char* DOWNLOAD_PATH = "/.crosspoint/dashboard/download.img";

  DashboardFetchResult fetch(dashboard::DashboardState& state) override;
  bool needsWifi() const override { return true; }
};

#endif  // CROSSINK_APP_CAP_DASHBOARD
