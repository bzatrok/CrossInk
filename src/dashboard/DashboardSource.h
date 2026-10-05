#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstdint>

#include "DashboardState.h"

// Where a dashboard image comes from. The timer-wake path calls fetch() once
// per wake. Implementations: LocalFolderSource and TrmnlSource.
enum class DashboardFetch : uint8_t { Updated, Unchanged, Failed };

struct DashboardFetchResult {
  DashboardFetch kind;
  char reason[24];  // short failure reason for the status line; empty otherwise
};

class DashboardSource {
 public:
  virtual ~DashboardSource() = default;
  // Writes /.crosspoint/dashboard/next.bmp and calls DashboardImageStore::publishNext() on success.
  virtual DashboardFetchResult fetch(dashboard::DashboardState& state) = 0;
  virtual bool needsWifi() const = 0;
};

// Server URL set -> TrmnlSource. Empty -> LocalFolderSource.
DashboardSource& selectDashboardSource();

// Helper for sources: a result with a copied reason.
DashboardFetchResult makeDashboardFetchResult(DashboardFetch kind, const char* reason = "");

#endif  // CROSSINK_APP_CAP_DASHBOARD
