#include "DashboardSource.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstring>

#include "CrossPointSettings.h"
#include "LocalFolderSource.h"
#include "TrmnlSource.h"

DashboardSource& selectDashboardSource() {
  static LocalFolderSource localFolder;
  static TrmnlSource trmnl;
  if (SETTINGS.dashboardServerUrl[0] != '\0') return trmnl;
  return localFolder;
}

DashboardFetchResult makeDashboardFetchResult(const DashboardFetch kind, const char* reason) {
  DashboardFetchResult result{kind, ""};
  strncpy(result.reason, reason ? reason : "", sizeof(result.reason) - 1);
  result.reason[sizeof(result.reason) - 1] = '\0';
  return result;
}

#endif  // CROSSINK_APP_CAP_DASHBOARD
