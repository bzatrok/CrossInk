#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include "DashboardSource.h"

// Offline dashboard source: cycles the *.bmp files in /dashboard/ on the SD
// root, in name order, one per fetch. It is the hardware test source and the
// offline option when no server URL is set.
class LocalFolderSource final : public DashboardSource {
 public:
  static constexpr const char* FOLDER = "/dashboard";

  DashboardFetchResult fetch(dashboard::DashboardState& state) override;
  bool needsWifi() const override { return false; }
};

#endif  // CROSSINK_APP_CAP_DASHBOARD
