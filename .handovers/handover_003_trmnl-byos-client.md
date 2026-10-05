# Handover 003 — TRMNL BYOS client

**Branch:** `feat/trmnl-byos-client` (create from `main` after [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) merges)
**Author:** session 2026-10-05  ·  **Status:** done (branched from `feat/home-control` with its PR into `feat/home-control`, at Ben's instruction)
**Log:** [handover_log.md](handover_log.md)
**Serves:** "shows a dashboard image from Ben's self-hosted TRMNL BYOS server".
**Scope:** Add a TRMNL BYOS image source behind the `DashboardSource` interface that
[dashboard-timer-wake](handover_002_dashboard-timer-wake.md) created. It provisions through `/api/setup`, polls
`/api/display`, downloads the image through a temp file, converts PNG to a 1-bit BMP, and reports Updated,
Unchanged or Failed. Handover 002 already owns waking, Wi-Fi, drawing, the banner, retries and settings.

## 0. Shared context (read first)
- Read [handover_log.md](handover_log.md), `PROJECT_CONTEXT.md`, `AGENTS.md`, `.claude/CONTEXT.md`, and the
  completed [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) first. Its "Interfaces this handover
  creates" section is the contract you implement against.
- Protocol reference: TRMNL docs `https://docs.trmnl.com/go/private-api/screens` and the Terminus API doc
  `https://github.com/usetrmnl/terminus/blob/main/doc/api.adoc`.
- Port reference (MIT, keep attribution): `https://github.com/wolodarskij/cross-trmnl`, files
  `src/network/TrmnlClient.{h,cpp}` and `src/network/WifiConnector.{h,cpp}`. Clone it to a scratch directory.
  Never copy from `DChells/crosspoint-x4-trmnl`: it has no license file.
- `lib/HomeControl/HomeControlHttp` is **not on `main`** (it lives on `feat/home-control`). Do not depend on it.
- **Claim gate:** OFF in this repo.

| Concern | Path:line |
|---|---|
| SDK HTTP client: `begin`, `addHeader`, `GET`, `GET(onData)`, `setTimeout`, `setInsecure`, `resolveUrl` | `freeink-sdk/libs/network/SecureNet/include/SecureHttpClient.h:68-130,328` |
| `HttpDownloader::downloadToFile` + `DownloadOptions` (`stageAsPart`, `validate`, `checkFreeSpace`) | `src/network/HttpDownloader.h:36-67,94-97` |
| Downloader timeouts (60 s request, 30 s idle), plain `http://` allowed | `src/network/HttpDownloader.cpp:31-35`, `src/network/HttpRedirectPolicy.h:32-33` |
| PNG → 1-bit BMP (Atkinson, always crops to exact target) | `lib/PngToBmpConverter/PngToBmpConverter.h:12-14`, `.cpp:211-236,900` |
| BMP parse (1/2/4/8/24/32 bpp, max 2048×3072) | `lib/GfxRenderer/Bitmap.h:67-70`, `Bitmap.cpp:112,132-135` |
| ArduinoJson 7.4.2 with filters | `platformio.ini:117`, example `lib/HomeControl/HueProtocol.cpp:33,67` on `feat/home-control` |
| Firmware version | `AppVersion::version()` (`lib/AppVersion/AppVersion.h:6`) |
| Battery millivolts | `BatteryMonitor::readMillivolts()` (`freeink-sdk/libs/hardware/BatteryMonitor/include/BatteryMonitor.h:61`), static instance pattern `lib/hal/HalPowerManager.cpp:202` |
| Board name | `BoardConfig::ACTIVE.name` |
| Storage ops | `Storage.exists/remove/rename/mkdir` `lib/hal/HalStorage.h:47,58-61` |
| Native test pattern | `test/CMakeLists.txt` (ArduinoJson + gtest via FetchContent), example dir `test/home_control_protocol/` on `feat/home-control` |

## 1 — Pure protocol layer  *(DECIDED)*
**Files:** `lib/Trmnl/TrmnlProtocol.h` · `lib/Trmnl/TrmnlProtocol.cpp` · `test/trmnl_protocol/CMakeLists.txt` ·
`test/trmnl_protocol/TrmnlProtocolTest.cpp` · `test/CMakeLists.txt` (add `add_subdirectory(trmnl_protocol)`)
- Include only `<ArduinoJson.h>` and standard headers. No Arduino, no HAL. This keeps it host-testable.
- Fixed-size outputs, no `std::string` in the structs:
  - `struct SetupResult { bool ok; char apiKey[64]; char friendlyId[16]; };`
  - `struct DisplayResult { bool ok; int status; char imageUrl[256]; char filename[96]; };`
- `bool parseSetup(const char* json, size_t len, SetupResult& out)` reads `api_key` and `friendly_id`.
- `bool parseDisplay(const char* json, size_t len, DisplayResult& out)` reads `status`, `image_url`, `filename`.
  It ignores `refresh_rate` (the local interval wins, see the log).
- `bool isNoChange(const DisplayResult& r, const char* lastFilename)`: true when `status == 202`, or when
  `filename` is non-empty and equals `lastFilename`.
- `bool resolveImageUrl(const char* base, const char* imageUrl, char* out, size_t cap)`: absolute URLs pass
  through, and relative URLs join to the base origin. Do it here, not with `SecureHttpClient::resolveUrl`, so it is testable.
- Tests cover: valid setup and display bodies, a missing `image_url`, `status` 0/200/202/500, a too-long URL
  (truncation fails, not silently cut), relative and absolute `image_url`, and the same-filename case.

## 2 — Transport client  *(DECIDED)*
**Files:** `lib/Trmnl/TrmnlClient.h` · `lib/Trmnl/TrmnlClient.cpp`
- Use `freeink::SecureHttpClient` directly. Port the shape of cross-trmnl `TrmnlClient.cpp:50-101,151-194`.
- Headers on every call:
  - `ID` = `WiFi.macAddress()`
  - `Access-Token` (when set)
  - `Battery-Voltage` in volts with two decimals, from `readMillivolts()`
  - `FW-Version` = `AppVersion::version()`
  - `RSSI` = `WiFi.RSSI()`
  - `Width` = 800 and `Height` = 480 (the panel's landscape size)
  - `Model` = `BoardConfig::ACTIVE.name`
- Timeout 10 s per JSON call (`setTimeout(10000)`). Read the body into a fixed 2 KB buffer through
  `GET(onData)`. Abort when it would overflow. Do not use the whole-body `String` path.
- `https://` uses `setInsecure()` for v1. Mark it with a `// TODO(trmnl-https)` comment that names the log's
  "Not yet specified" entry. `http://` works as is.
- Check heap before a TLS call with the cross-trmnl floor: free heap and largest block ≥ 55000 bytes
  (`TrmnlClient.cpp:28,36-45`). Skip the check for `http://`.
- Read the URL and key from `SETTINGS.dashboardServerUrl` and `SETTINGS.dashboardApiKey` (created by handover 002).
- Provisioning: when `dashboardApiKey` is empty, call `/api/setup`. On success, copy the key into
  `SETTINGS.dashboardApiKey`, call `SETTINGS.saveToFile()`, log the friendly ID with `LOG_INF`, and continue in the
  same wake. The friendly ID is not stored.

## 3 — `TrmnlSource` behind `DashboardSource`  *(DECIDED)*
**Files:** `src/dashboard/TrmnlSource.h` · `src/dashboard/TrmnlSource.cpp` · `src/dashboard/DashboardSource.cpp` (`selectDashboardSource`)
- Implement `DashboardSource` exactly as declared in handover 002 step 4. `needsWifi()` returns true.
- Selection rule in `selectDashboardSource()`: `dashboardServerUrl` set → `TrmnlSource`. Empty → `LocalFolderSource`.
- Flow in `fetch()`:
  1. Provision if needed.
  2. Call `/api/display`.
  3. If `isNoChange(result, state.lastFilename)`, return `Unchanged`.
  4. Else download `image_url` with `HttpDownloader::downloadToFile` to `/.crosspoint/dashboard/download.img`.
     Use `stageAsPart = true` and `checkFreeSpace = false`.
  5. Detect the format from the magic bytes (`BM` or `\x89PNG`), as cross-trmnl `TrmnlClient.cpp:105-145` does.
  6. Produce the next image:
     - A BMP is kept if `Bitmap::parseHeaders()` accepts it.
     - A PNG goes through `pngFileTo1BitBmpStreamWithSize(in, out, 800, 480)` into `/.crosspoint/dashboard/next.bmp`.
     - Pass exactly 800×480, never a square target: the converter always crops to the target.
     Write the BMP case by renaming `download.img` to `next.bmp`.
  7. Call `DashboardImageStore::publishNext()`. If it fails, return `Failed("format")`. Else copy `filename` into
     `state.lastFilename` and return `Updated`. Handover 002's wake path saves the state.
- Every failure returns `Failed` with a short reason (`"setup"`, `"display 500"`, `"download"`, `"format"`), and
  logs with `LOG_ERR`. The current image is never touched on failure.
- Remove `download.img` and any `.part` file in every exit path.

## 4 — Docs and changelog  *(DECIDED)*
**Files:** `docs/dashboard.md` (created by handover 002, add a "TRMNL BYOS server" section) ·
`docs/file-formats.md` (the `/.crosspoint/dashboard/` files) · `CHANGELOG.md` (`## [Unreleased]` → `### Added`)
- The docs section covers: the headers sent, provisioning, that `refresh_rate` is ignored, the PNG crop rule, and
  that `https://` does not yet check certificates.
- Credit cross-trmnl (MIT) in `docs/dashboard.md` and in a header comment in `TrmnlClient.cpp`.

## Sequencing
1 (host tests green) → 2 → 3 → simulator check → 4 → acceptance.

## Decisions — locked ✅
- A new `lib/Trmnl` on `SecureHttpClient`. Rejected: `HomeControlHttp`, because it is not on `main` and allows only one extra header.
- A pure `TrmnlProtocol` with native tests. Rejected: parsing inside the transport, because it cannot be tested on the host.
- A fixed 2 KB JSON body buffer with abort on overflow. Rejected: `getString()` into a heap `String` (heap churn, no bound).
- A PNG target of exactly 800×480. Rejected: cross-trmnl's `maxDim×maxDim`, which crops landscape images on this converter.
- 1-bit output only. Rejected: cross-trmnl's 2-bit converter variant (grayscale is out of scope, see the log).
- Battery voltage and model headers added. Rejected: cross-trmnl's header set, which shows no battery on the server.
- `https://` without certificate checks in v1, behind a TODO. Rejected: blocking v1 on certificate handling.
- No BLE stop before Wi-Fi. Rejected: cross-trmnl's `bleinput::stop()`, because CrossInk has no BLE stack.

## Out of scope for this handover
- `/api/log` reporting. Not needed for v1.
- Honouring the server's `refresh_rate`. The local interval wins (see the log).
- Anything in handover 002's area: wake, Wi-Fi connect, drawing, banner, retries, settings UI.

## Acceptance / pre-merge checklist
- `cmake -S test -B test/build && cmake --build test/build --target TrmnlProtocolTest && ctest --test-dir test/build -R Trmnl` passes.
- `~/.platformio/penv/bin/pio run -e x4-pro` builds. `~/.platformio/penv/bin/pio run -e default` builds and
  contains no TRMNL code (check `nm` or the map file for `TrmnlClient`).
- `x4-pro-simulator` with a local BYOS server:
  - Run `docker run` for Terminus or byos_next on the Mac, following its README.
  - Set the Server URL to `http://<mac-lan-ip>:<port>`. Use "Refresh now" in the Dashboard tab.
  - Expected: the device provisions, the server lists it, and the image appears.
  - A second "Refresh now" with no server change returns `Unchanged`. The log shows no download.
- Hardware check for Ben, in the PR body:
  - Same server on the LAN. Set the interval to 1 min.
  - Change the playlist on the server. The panel updates within one interval, with no flash.
  - Stop the server. The banner appears on the next wake, and the old image stays.

---
**On completion:**
1. Update `.handovers/handover_log.md`. Set this handover's status to ✅ done and fill in the Completed date. If you
   could not finish, set 🔄 in-progress and add a note row that describes what remains.
2. Add each locked decision to the log's **Decisions so far**. Write one line: the gist plus the rejected
   alternative, linking this doc by name. The detail stays here.
3. Move anything this work made sharp out of **Not yet specified**. Move anything it ruled out into **Out of scope**.
4. Commit, push the branch, and open the PR (to `bzatrok/CrossInk`, base `main`).
