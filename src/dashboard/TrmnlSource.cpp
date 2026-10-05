#include "TrmnlSource.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <HalStorage.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <TrmnlClient.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "DashboardImageStore.h"
#include "network/HttpDownloader.h"

namespace {

constexpr const char* DOWNLOAD_PART_PATH = "/.crosspoint/dashboard/download.img.part";
// Exactly the panel size in the image's own orientation: the converter always
// crops to its target, so a square or mismatched target cuts the image.
constexpr int PANEL_LONG_SIDE = 800;
constexpr int PANEL_SHORT_SIDE = 480;

enum class ImageFormat : uint8_t { Unknown, Bmp, Png };

// Removes the download and its staging file on every exit path.
struct DownloadCleanup {
  ~DownloadCleanup() {
    if (Storage.exists(TrmnlSource::DOWNLOAD_PATH)) Storage.remove(TrmnlSource::DOWNLOAD_PATH);
    if (Storage.exists(DOWNLOAD_PART_PATH)) Storage.remove(DOWNLOAD_PART_PATH);
  }
};

DashboardFetchResult failed(const char* reason) {
  LOG_ERR("TRMNL", "Fetch failed: %s", reason);
  return makeDashboardFetchResult(DashboardFetch::Failed, reason);
}

// Server URL without trailing slashes. False when it does not fit.
bool copyBaseUrl(char* out, size_t cap) {
  const size_t len = strnlen(SETTINGS.dashboardServerUrl, sizeof(SETTINGS.dashboardServerUrl));
  if (len == 0 || len >= cap) return false;
  std::memcpy(out, SETTINGS.dashboardServerUrl, len);
  size_t end = len;
  while (end > 0 && out[end - 1] == '/') --end;
  out[end] = '\0';
  return end > 0;
}

bool provision(const char* baseUrl) {
  trmnl::SetupResult setup{};
  const int status = trmnl::requestSetup(baseUrl, setup);
  if (status != 200 || !setup.ok) {
    LOG_ERR("TRMNL", "/api/setup -> %d", status);
    return false;
  }
  strncpy(SETTINGS.dashboardApiKey, setup.apiKey, sizeof(SETTINGS.dashboardApiKey) - 1);
  SETTINGS.dashboardApiKey[sizeof(SETTINGS.dashboardApiKey) - 1] = '\0';
  if (!SETTINGS.saveToFile()) LOG_ERR("TRMNL", "Could not save the provisioned API key");
  LOG_INF("TRMNL", "Provisioned as %s", setup.friendlyId[0] != '\0' ? setup.friendlyId : "(no friendly id)");
  return true;
}

ImageFormat sniffFormat(const char* path) {
  FsFile file;
  if (!Storage.openFileForRead("TRMNL", path, file)) return ImageFormat::Unknown;
  uint8_t magic[4] = {0, 0, 0, 0};
  const int read = file.read(magic, sizeof(magic));
  file.close();
  if (read >= 2 && magic[0] == 'B' && magic[1] == 'M') return ImageFormat::Bmp;
  if (read == 4 && magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') return ImageFormat::Png;
  LOG_ERR("TRMNL", "Unknown image format (magic %02x %02x)", magic[0], magic[1]);
  return ImageFormat::Unknown;
}

// Reads width and height from the PNG IHDR chunk (bytes 16-23, big-endian).
bool readPngSize(FsFile& png, uint32_t& width, uint32_t& height) {
  uint8_t head[24];
  if (png.read(head, sizeof(head)) != static_cast<int>(sizeof(head)) || !png.seek(0)) return false;
  width = (uint32_t{head[16]} << 24) | (uint32_t{head[17]} << 16) | (uint32_t{head[18]} << 8) | head[19];
  height = (uint32_t{head[20]} << 24) | (uint32_t{head[21]} << 16) | (uint32_t{head[22]} << 8) | head[23];
  return width > 0 && height > 0;
}

bool convertPngToNext() {
  FsFile png;
  if (!Storage.openFileForRead("TRMNL", TrmnlSource::DOWNLOAD_PATH, png)) return false;
  uint32_t width = 0;
  uint32_t height = 0;
  if (!readPngSize(png, width, height)) {
    LOG_ERR("TRMNL", "PNG header unreadable");
    png.close();
    return false;
  }
  const bool tall = width < height;
  const int targetWidth = tall ? PANEL_SHORT_SIDE : PANEL_LONG_SIDE;
  const int targetHeight = tall ? PANEL_LONG_SIDE : PANEL_SHORT_SIDE;
  FsFile bmp;
  if (!Storage.openFileForWrite("TRMNL", DashboardImageStore::NEXT_BMP, bmp)) {
    png.close();
    return false;
  }
  const bool ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(png, bmp, targetWidth, targetHeight);
  bmp.close();
  png.close();
  if (!ok) {
    LOG_ERR("TRMNL", "PNG conversion failed");
    Storage.remove(DashboardImageStore::NEXT_BMP);
  }
  return ok;
}

bool moveBmpToNext() {
  if (Storage.exists(DashboardImageStore::NEXT_BMP)) Storage.remove(DashboardImageStore::NEXT_BMP);
  if (Storage.rename(TrmnlSource::DOWNLOAD_PATH, DashboardImageStore::NEXT_BMP)) return true;
  LOG_ERR("TRMNL", "Could not move the download to next.bmp");
  return false;
}

}  // namespace

DashboardFetchResult TrmnlSource::fetch(dashboard::DashboardState& state) {
  DownloadCleanup cleanup;
  char baseUrl[sizeof(SETTINGS.dashboardServerUrl)];
  if (!copyBaseUrl(baseUrl, sizeof(baseUrl))) return failed("server url");

  // 1. Provision if needed. The key is used in this same wake.
  if (SETTINGS.dashboardApiKey[0] == '\0' && !provision(baseUrl)) return failed("setup");

  // 2. Poll /api/display.
  trmnl::DisplayResult display{};
  const int httpStatus = trmnl::requestDisplay(baseUrl, SETTINGS.dashboardApiKey, display);
  char reason[sizeof(DashboardFetchResult::reason)];
  if (httpStatus == 202) return makeDashboardFetchResult(DashboardFetch::Unchanged);
  if (httpStatus != 200) {
    snprintf(reason, sizeof(reason), "display %d", httpStatus);
    return failed(reason);
  }
  if (!display.ok) return failed("display json");

  // 3. Nothing new: skip the download and the redraw.
  if (trmnl::isNoChange(display, state.lastFilename)) {
    LOG_INF("TRMNL", "No change (%s)", display.filename);
    return makeDashboardFetchResult(DashboardFetch::Unchanged);
  }
  if (display.status != 0 && display.status != 200) {
    snprintf(reason, sizeof(reason), "display %d", display.status);
    return failed(reason);
  }

  // 4. Download into a staged temp file.
  char imageUrl[sizeof(display.imageUrl)];
  if (!trmnl::resolveImageUrl(baseUrl, display.imageUrl, imageUrl, sizeof(imageUrl))) return failed("image url");
  if (!DashboardImageStore::ensureDirectory()) return failed("download");
  if (!trmnl::heapOkForUrl(imageUrl)) return failed("download");
  LOG_INF("TRMNL", "Fetching %s", imageUrl);
  HttpDownloader::DownloadOptions options;
  options.stageAsPart = true;
  options.checkFreeSpace = false;
  if (HttpDownloader::downloadToFile(imageUrl, DOWNLOAD_PATH, nullptr, nullptr, "", "", options) !=
      HttpDownloader::OK) {
    return failed("download");
  }

  // 5-6. Detect the format and produce next.bmp.
  bool produced = false;
  switch (sniffFormat(DOWNLOAD_PATH)) {
    case ImageFormat::Bmp:
      produced = moveBmpToNext();
      break;
    case ImageFormat::Png:
      produced = convertPngToNext();
      break;
    case ImageFormat::Unknown:
      break;
  }
  // 7. publishNext() parses next.bmp and keeps current.bmp on any failure.
  if (!produced || !DashboardImageStore::publishNext()) return failed("format");

  strncpy(state.lastFilename, display.filename, sizeof(state.lastFilename) - 1);
  state.lastFilename[sizeof(state.lastFilename) - 1] = '\0';
  return makeDashboardFetchResult(DashboardFetch::Updated);
}

#endif  // CROSSINK_APP_CAP_DASHBOARD
