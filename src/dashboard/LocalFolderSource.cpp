#include "LocalFolderSource.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <strings.h>

#include "DashboardImageStore.h"

namespace {
constexpr size_t NAME_MAX_LEN = sizeof(dashboard::DashboardState::lastFilename);

bool isBmpName(const char* name) {
  if (name[0] == '.') return false;  // hidden files and macOS ._ sidecars
  const size_t len = strlen(name);
  return len > 4 && strcasecmp(name + len - 4, ".bmp") == 0;
}

// One directory pass without building a list: the smallest name overall and
// the smallest name after `after`. Both buffers are NAME_MAX_LEN bytes.
struct FolderScan {
  uint16_t count = 0;
  char first[NAME_MAX_LEN] = "";
  char next[NAME_MAX_LEN] = "";
};

bool scanFolder(const char* after, FolderScan& scan) {
  FsFile dir = Storage.open(LocalFolderSource::FOLDER);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }
  char name[NAME_MAX_LEN];
  for (FsFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const bool isFile = !entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();
    if (!isFile || !isBmpName(name)) continue;
    ++scan.count;
    if (scan.first[0] == '\0' || strcmp(name, scan.first) < 0) strcpy(scan.first, name);
    if (strcmp(name, after) > 0 && (scan.next[0] == '\0' || strcmp(name, scan.next) < 0)) strcpy(scan.next, name);
  }
  dir.close();
  return true;
}

// Position of `name` in name order, for the state file.
uint16_t rankOf(const char* name) {
  FsFile dir = Storage.open(LocalFolderSource::FOLDER);
  if (!dir) return 0;
  uint16_t rank = 0;
  char entryName[NAME_MAX_LEN];
  for (FsFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const bool isFile = !entry.isDirectory();
    entry.getName(entryName, sizeof(entryName));
    entry.close();
    if (isFile && isBmpName(entryName) && strcmp(entryName, name) < 0) ++rank;
  }
  dir.close();
  return rank;
}

bool copyToNext(const char* name) {
  char path[NAME_MAX_LEN + 16];
  snprintf(path, sizeof(path), "%s/%s", LocalFolderSource::FOLDER, name);
  FsFile in;
  if (!Storage.openFileForRead("DSH", path, in)) return false;
  FsFile out;
  if (!Storage.openFileForWrite("DSH", DashboardImageStore::NEXT_BMP, out)) {
    in.close();
    return false;
  }
  // Static: 1 KB chunks keep SD throughput reasonable without a stack-heavy buffer.
  static uint8_t chunk[1024];
  bool ok = true;
  int read = 0;
  while ((read = in.read(chunk, sizeof(chunk))) > 0) {
    if (out.write(chunk, static_cast<size_t>(read)) != static_cast<size_t>(read)) {
      ok = false;
      break;
    }
  }
  if (read < 0) ok = false;
  in.close();
  out.close();
  if (!ok) {
    LOG_ERR("DSH", "Copy of %s to next.bmp failed", path);
    Storage.remove(DashboardImageStore::NEXT_BMP);
  }
  return ok;
}
}  // namespace

DashboardFetchResult LocalFolderSource::fetch(dashboard::DashboardState& state) {
  FolderScan scan;
  if (!scanFolder(state.lastFilename, scan) || scan.count == 0) {
    return makeDashboardFetchResult(DashboardFetch::Failed, "no images");
  }
  if (scan.count == 1 && strcmp(scan.first, state.lastFilename) == 0 && DashboardImageStore::hasCurrent()) {
    return makeDashboardFetchResult(DashboardFetch::Unchanged);
  }

  // Wrap to the first name after the last one.
  const char* chosen = scan.next[0] != '\0' ? scan.next : scan.first;
  if (!DashboardImageStore::ensureDirectory() || !copyToNext(chosen)) {
    return makeDashboardFetchResult(DashboardFetch::Failed, "copy failed");
  }
  if (!DashboardImageStore::publishNext()) {
    return makeDashboardFetchResult(DashboardFetch::Failed, "bad image");
  }
  strncpy(state.lastFilename, chosen, sizeof(state.lastFilename) - 1);
  state.lastFilename[sizeof(state.lastFilename) - 1] = '\0';
  state.localFolderIndex = rankOf(chosen);
  return makeDashboardFetchResult(DashboardFetch::Updated);
}

#endif  // CROSSINK_APP_CAP_DASHBOARD
