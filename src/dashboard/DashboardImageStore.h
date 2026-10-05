#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include <cstddef>
#include <cstdint>

// SD layout for Dashboard sleep. A new image is written to next.bmp and only
// replaces current.bmp after it parses as a bitmap, so a partial or broken
// download never replaces the last good image.
namespace DashboardImageStore {

constexpr const char* DIR = "/.crosspoint/dashboard";
constexpr const char* CURRENT_BMP = "/.crosspoint/dashboard/current.bmp";
constexpr const char* NEXT_BMP = "/.crosspoint/dashboard/next.bmp";
constexpr const char* BACKUP_BMP = "/.crosspoint/dashboard/current.bmp.old";
constexpr const char* FRAME_BIN = "/.crosspoint/dashboard/frame.bin";
constexpr const char* STATE_JSON = "/.crosspoint/dashboard/state.json";

bool ensureDirectory();
bool hasCurrent();

// Validates next.bmp, then swaps it over current.bmp. On any failure
// current.bmp is untouched and next.bmp is removed.
bool publishNext();

// The framebuffer that is on the glass, so the next wake can restore it.
bool saveFrame(const uint8_t* buffer, size_t size);
// True only when frame.bin exists with exactly `size` bytes and reads fully.
bool loadFrame(uint8_t* buffer, size_t size);

}  // namespace DashboardImageStore

#endif  // CROSSINK_APP_CAP_DASHBOARD
