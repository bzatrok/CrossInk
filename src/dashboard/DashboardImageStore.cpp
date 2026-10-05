#include "DashboardImageStore.h"

#if CROSSINK_APP_CAP_DASHBOARD

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>

namespace DashboardImageStore {

namespace {
// A crash between the two renames leaves only the backup; restore it.
bool recoverBackup() {
  if (!Storage.exists(BACKUP_BMP)) return true;
  const bool ok = Storage.exists(CURRENT_BMP) ? Storage.remove(BACKUP_BMP) : Storage.rename(BACKUP_BMP, CURRENT_BMP);
  if (!ok) LOG_ERR("DSH", "Could not recover %s", BACKUP_BMP);
  return ok;
}

bool nextParses() {
  FsFile file;
  if (!Storage.openFileForRead("DSH", NEXT_BMP, file)) return false;
  Bitmap bitmap(file);
  const BmpReaderError err = bitmap.parseHeaders();
  file.close();
  if (err != BmpReaderError::Ok) {
    LOG_ERR("DSH", "Rejected next.bmp: %s", Bitmap::errorToString(err));
    return false;
  }
  return true;
}
}  // namespace

bool ensureDirectory() {
  if (Storage.ensureDirectoryExists(DIR)) return true;
  LOG_ERR("DSH", "Could not create %s", DIR);
  return false;
}

bool hasCurrent() {
  recoverBackup();
  return Storage.exists(CURRENT_BMP);
}

bool publishNext() {
  if (!nextParses()) {
    Storage.remove(NEXT_BMP);
    return false;
  }
  if (!recoverBackup()) return false;
  const bool replacing = Storage.exists(CURRENT_BMP);
  if (replacing && !Storage.rename(CURRENT_BMP, BACKUP_BMP)) {
    LOG_ERR("DSH", "Could not preserve current.bmp");
    return false;
  }
  if (!Storage.rename(NEXT_BMP, CURRENT_BMP)) {
    LOG_ERR("DSH", "Could not publish next.bmp");
    if (replacing && !Storage.rename(BACKUP_BMP, CURRENT_BMP)) LOG_ERR("DSH", "current.bmp left in backup");
    return false;
  }
  // Cleanup failure does not invalidate the published image; recoverBackup() retries later.
  if (replacing && !Storage.remove(BACKUP_BMP)) LOG_ERR("DSH", "Could not remove %s", BACKUP_BMP);
  return true;
}

bool saveFrame(const uint8_t* buffer, const size_t size) {
  if (!ensureDirectory()) return false;
  FsFile file;
  if (!Storage.openFileForWrite("DSH", FRAME_BIN, file)) return false;
  const size_t written = file.write(buffer, size);
  file.close();
  if (written != size) {
    LOG_ERR("DSH", "Short frame write: %u/%u", static_cast<unsigned>(written), static_cast<unsigned>(size));
    Storage.remove(FRAME_BIN);
    return false;
  }
  return true;
}

bool loadFrame(uint8_t* buffer, const size_t size) {
  if (!Storage.exists(FRAME_BIN)) return false;
  FsFile file;
  if (!Storage.openFileForRead("DSH", FRAME_BIN, file)) return false;
  if (file.fileSize() != size) {
    LOG_ERR("DSH", "frame.bin size %u does not match framebuffer %u", static_cast<unsigned>(file.fileSize()),
            static_cast<unsigned>(size));
    file.close();
    return false;
  }
  const int read = file.read(buffer, size);
  file.close();
  if (read != static_cast<int>(size)) {
    LOG_ERR("DSH", "Short frame read");
    return false;
  }
  return true;
}

}  // namespace DashboardImageStore

#endif  // CROSSINK_APP_CAP_DASHBOARD
