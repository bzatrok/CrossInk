# Handover 005 — Tailnet on the X4 Pro (direct path)

**Branch:** CrossInk `feat/tailnet-client` (create from `main`). In `tailesp32`: `feat/esp-idf-port` (create from
`main` after [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) merges).  ·  **Author:** session
2026-10-07  ·  **Status:** pending
**Log:** [log_tailnet.md](log_tailnet.md)
**Serves:** "Any HTTP request to a tailnet address reaches its server", at home over the direct path. Travelling
needs [tailesp32-derp](handover_006_tailesp32-derp.md).
**Scope:** Add an ESP-IDF port and an lwIP network interface to `tailesp32`. Pull `tailesp32` into CrossInk as a
submodule for the `x4-pro` builds. Start the tunnel when Wi-Fi gets an IP and stop it with Wi-Fi. Add a web-only
auth key setting and a status line. Make the dashboard wait for the tunnel before it fetches a 100.x URL. Point
the dashboard at Terminus's 100.x address and prove it on hardware.

## 0. Shared context (read first)

- Read [log_tailnet.md](log_tailnet.md), [PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md), `CLAUDE.md` (→ `AGENTS.md`)
  and `.claude/CONTEXT.md`. `pio` is at `~/.platformio/penv/bin/pio`. Flashing: `docs/development/fork-workflow.md`.
- **Prerequisite:** [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) is ✅ in the log. Stop if not.
- **Prerequisite for the hardware checks only:** Ben's Mac is on the `bzatrok.github` tailnet, or Terminus already
  runs on AMB-HM-001 (Amberglass.Infra handover `terminus-home-vm`). Check with `tailscale status`. If neither
  holds, finish everything else, then stop and ask Ben in the PR body. Do not switch the Mac's tailnet yourself.
- **Two repos, two rules.** CrossInk commits keep the normal attribution trailers. `tailesp32` commits and its PR
  follow the no-AI-footprint Invariant in the log: no trailers, no "Generated with", no handover number in titles.
- Key files:

  | Concern | Path:line |
  |---|---|
  | Timer-wake branch (register the Wi-Fi hook before it) | `src/main.cpp:1412-1421` |
  | Wi-Fi event logging, the template for a global hook | `src/activities/network/WifiSelectionActivity.cpp:98-106` |
  | Foreground connect, used by every network feature | `src/activities/network/WifiSelectionActivity.cpp:648-707` |
  | Dashboard Wi-Fi connect and disconnect | `src/network/WifiConnector.cpp:52`, `:100`; `src/dashboard/DashboardWake.cpp:169-172`, `:222` |
  | Dashboard fetch, 10 s JSON timeout | `src/dashboard/TrmnlSource.cpp:125-161`; `lib/Trmnl/TrmnlClient.cpp:25-26`, `:79-83` |
  | 60 s backstop | `src/dashboard/DashboardWake.h:21`; `src/dashboard/DashboardWake.cpp:39-60`, `:138` |
  | Secret setting field and registration | `src/CrossPointSettings.h:703`; `src/SettingsList.cpp:277-285` |
  | Obfuscation on SD | `src/CrossPointSettings.cpp:447-450`, `:537-542`; `lib/Serialization/ObfuscationUtils.h:34-42` |
  | Web GET masks secrets | `src/network/CrossPointWebServer.cpp:1678` |
  | Web POST keeps a secret on empty | `src/network/CrossPointWebServer.cpp:1781` |
  | Password-type input in the web page | `web/pages/settings.js:51-55` |
  | Device settings tabs (a key listed in none is web-only) | `src/SettingsList.h:595`, `:1124` |
  | Capability flag validation | `include/AppCapabilities.h:33-42` |
  | Submodule plus `symlink://` precedent | `.gitmodules`; `platformio.ini:99-114`, `:415` |
  | Code on the tcpip thread, precedent | `lib/hal/HalClock.cpp:253-270` |
  | NVS partition, 20 KB | `partitions.csv:2` |

- Every network client in the app uses lwIP sockets (`WiFiClient`, `SecureHttpClient`, `esp_http_client`). A netif
  that owns 100.64.0.0/10 routes all of them with no change to OPDS, KOReader sync or the dashboard client.
- **Claim gate:** ON, registered globally in `~/.claude/settings.json` (a Stop hook running
  `~/.claude/hooks/review-gate.sh`). On every turn that changes a file, a fresh reviewer compares your closing
  message against the diff. A contradicted claim blocks the turn and the reason comes back on stderr. Full reviews
  land under `~/.claude/reviews/`. The gate releases itself after 3 blocks in a row. When blocked, correct the work
  or correct the claim. Never work around the gate. The acceptance checklist is the set of claims your final turn
  is judged against.

## 1 — ESP-IDF port in tailesp32  *(DECIDED)*

**Files:** `ports/esp-idf/tse_port_esp.c`, `ports/esp-idf/tse_netif.c`, `include/tailesp32/tailesp32_esp.h`.
- Platform: lwIP sockets (`lwip/sockets.h`), `esp_random`, `esp_timer_get_time`, `time()` for wall clock,
  NVS namespace `tailesp32` with blobs `keys` and `peers`, `esp_log` tag `tailesp32`.
- `alloc_large` uses `heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`. On failure it returns NULL and the caller logs and
  fails the slow path. It never falls back to internal RAM.
- `tse_netif`: an lwIP netif adapted from wireguard-lwip `wireguardif.c` (keep its BSD-3 header). Address = own
  100.x, netmask 255.192.0.0, no gateway, never the default netif. Output hands the packet to `tse_send_ip`.
- One FreeRTOS task, 4096-byte stack (the AGENTS.md figure for network work), internal RAM. It loops on
  `tse_poll` with a 1 s timeout, feeds packets to lwIP with `tcpip_inpkt`, and calls `tse_tick`.
- Add and remove the netif on the tcpip thread (`tcpip_callback`, or `LOCK_TCPIP_CORE` when core locking is
  enabled in the Arduino lwIP config).
- API: `tse_esp_start()`, `tse_esp_stop()`, `tse_esp_wait_ready(timeout_ms)`, `tse_esp_register(auth_key)`.
  Each logs and returns false on failure. None aborts.

## 2 — Pull tailesp32 into CrossInk  *(DECIDED)*

- `git submodule add -b main https://github.com/bzatrok/tailesp32.git tailesp32`. HTTPS URL, as `freeink-sdk`.
  The repo is private until Ben publishes it, so this needs Ben's GitHub credentials, which the Mac has.
- `lib_deps` gains `Tailesp32=symlink://tailesp32` in `[env:x4-pro]` and `[env:x4-pro-debug]` only.
  `x4-pro-latency` inherits it from `x4-pro`.
- `CROSSINK_APP_CAP_TAILNET`: `=1` in `x4-pro` and `x4-pro-debug`, `=0` in every other environment, including
  `x4-pro-simulator`. Add the validation block in `include/AppCapabilities.h` after DASHBOARD.
- `.claude/CONTEXT.md`: one line under Fork Tooling, beside the `freeink-sdk` line, naming the submodule and its
  branch.

## 3 — TailnetService  *(DECIDED)*

**Files:** `src/network/TailnetService.{h,cpp}`, all under `#if CROSSINK_APP_CAP_TAILNET`.
- `TailnetService::begin()` registers one `WiFi.onEvent` handler. Call it in `setup()` before the timer-wake
  branch at `src/main.cpp:1412`, so dashboard wakes get it too.
- On `ARDUINO_EVENT_WIFI_STA_GOT_IP`: if `SETTINGS.tailnetAuthKey` is set, register (slow path). On success,
  clear the key and save settings. Otherwise, when registered, start the tunnel (fast path).
- On `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` and `ARDUINO_EVENT_WIFI_STA_LOST_IP`: stop the tunnel and free everything.
  AP mode and ESP-NOW (Nearby sync) never produce `GOT_IP`, so they never start it.
- Slow-path triggers: no cache, a fast-path failure (refresh once, then retry once), or a cache older than 24 h.
  A stale cache does not block: the fast path runs from it first, and the refresh runs afterwards in the tunnel
  task. In a dashboard wake, the refresh runs after the image fetch and before sleep.
- `TailnetService::waitReady(timeoutMs)` returns true when the tunnel is up or not needed.
- RAM: everything is allocated on start and freed on stop. With Wi-Fi off there is no tunnel and no tailnet RAM.

## 4 — Dashboard waits for the tunnel  *(DECIDED)*

- In `TrmnlSource` (`src/dashboard/TrmnlSource.cpp`, after `copyBaseUrl` at `:125-126`): when the URL host is in
  100.64.0.0/10, call `TailnetService::waitReady(3000)`, or `waitReady(12000)` when a slow path is running. On
  timeout, return a failed result with log `tailnet not ready`. The existing retry and banner logic handles it.
- The 60 s backstop stays as is. 12 s plus the 10 s JSON timeout plus the image download stays well inside it.

## 5 — Settings and web portal  *(DECIDED)*

- `char tailnetAuthKey[96]` in `CrossPointSettings`, under the flag. Registered in `SettingsList.cpp` with
  `.withObfuscated()`, category `STR_CAT_SYSTEM`, and listed in no device tab builder, so it is web-only.
- Mask it in the settings GET beside `dashboardApiKey` (`CrossPointWebServer.cpp:1678`). Keep it on an empty POST
  (`:1781`).
- `web/pages/settings.js:51-55`: render it as a password input. Match on the setting key, not the display name.
- Add a read-only `tailnetStatus` value to the settings GET: "Not registered", "Registered as 100.x.y.z, cache 3 h",
  or "Failed: <reason>". Show it under the key field. No new endpoint. Update `docs/webserver-endpoints.md` if it
  lists GET fields.
- Strings: `STR_TAILNET_AUTH_KEY` and `STR_TAILNET_STATUS` in `lib/I18n/translations/english.yaml`. Run
  `scripts/gen_i18n.py` (also a `pre:` script). Never edit the generated files.
- Rebuild the web headers with `scripts/build_web.py`. Never edit `*.generated.h`.

## 6 — Docs and changelog  *(DECIDED)*

- `docs/tailnet.md`, front matter `nav_order: 18`: what it does, the Tailscale admin setup (tag, ACL, reusable
  pre-approved key), entering the key in the web portal, typing a `http://100.x.y.z:port` Server URL, limits
  (direct path only until DERP, IPv4, no MagicDNS), and how to re-register.
- Link it from `docs/index.md` beside Home Control.
- `CHANGELOG.md` under `## [Unreleased]` → `### Added`: one user-facing line.

## 7 — Point the dashboard at the tailnet  *(DECIDED)*

Terminus builds image links from its `API_URI`. The device fetches those links, so both must use the 100.x address.
- Find the Terminus host's 100.x address with `tailscale status`. It is the Mac, or `amb-hm-001` if Terminus has
  moved.
- On that host, set `API_URI=http://<100.x>:2300` in Terminus's `.env` (`~/dev/terminus/.env` on the Mac,
  `/opt/terminus/.env` on the VM), then `docker compose up -d` in that directory.
- On the device web portal, set Server URL to `http://<100.x>:2300` and enter the reusable auth key from
  `~/.config/tailesp32/.env`.

## Sequencing

1, then 2 (build only), then 3, 4, 5, then flash and run the hardware checks, then 6 and 7. Task 7 changes a live
server setting, so do it last, right before the hardware checks.

## Decisions — locked ✅

- One global Wi-Fi event hook in `main.cpp`. Rejected: hooks in each activity, and a lazy start on the first
  100.x connection.
- The netif owns 100.64.0.0/10 and is never the default netif. Rejected: per-client binding to the tunnel IP.
- Big buffers live only in PSRAM, with no internal-RAM fallback. Rejected: a fallback that can starve the reader.
- The auth key is a web-only obfuscated setting that clears after registration. Rejected: an SD file, and on-device entry.
- The status is a read-only field in the existing settings GET. Rejected: a new endpoint.
- No change to OPDS, KOReader sync or `lib/Trmnl`. Rejected: tunnel-aware clients.
- The simulator does not get the tunnel. Rejected: wiring the POSIX port into `x4-pro-simulator` (not needed to prove the device).

## Out of scope for this handover

- DERP and travel. See [tailesp32-derp](handover_006_tailesp32-derp.md).
- OPDS and KOReader sync over the tailnet. Nothing serves them there until Grimmory's 6060 is bound (Amberglass.Infra
  `terminus-home-vm`). Do not test them.
- Moving Terminus. That is the Amberglass.Infra handover.
- A device-side screen for the tailnet.

## Acceptance / pre-merge checklist

- `tailesp32`: native `ctest` still passes.
- CrossInk builds: `pio run -e x4-pro`, `-e x4-pro-debug`, `-e default`, `-e sticky`, `-e simulator`,
  `-e x4-pro-simulator`. The non-X4-Pro builds contain no tailnet code (check the link map for `tse_`).
- Static RAM: compare `pio run -e x4-pro` DRAM before and after. Report the delta in the PR. Over 4 KB of static
  internal RAM fails the check.
- `pio check -e x4-pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` on the touched files.
- Run clang-format on touched C++ files.
- Hardware, X4 Pro at home:
  - Enter the auth key in the web portal. Within 10 s the status shows "Registered as 100.x". The key field is
    empty after reload. Ben sees the device with `tag:xteink` in the admin console (ask in the PR body).
  - With Server URL `http://<100.x>:2300`, three timer wakes refresh the dashboard. The serial log shows the time
    from `GOT_IP` to tunnel ready. Report all three. The fast path adds at most 1000 ms.
  - Wi-Fi off and on in a foreground network screen: the log shows the tunnel stop and start, with no leak in
    `ESP.getFreeHeap()` over 3 cycles.
  - Open a book with Wi-Fi off: free heap matches the `.claude/CONTEXT.md` baseline (about 85-90 KB free).
- `git status --short` shows no `.pio/`, `*.generated.h`, `compile_commands.json` or `platformio.local.ini`.

---
**On completion:**
1. Update `.handovers/log_tailnet.md`. Set this handover's status to ✅ done and fill in the Completed date. If you
   could not finish, set 🔄 in-progress and add a note row that describes what remains.
2. Add each locked decision to the log's **Decisions so far**. Write one line: the gist plus the rejected
   alternative, linking this doc by name. The detail stays here.
3. Move anything this work made sharp out of **Not yet specified**. Move anything it ruled out into **Out of scope**.
4. Commit, push, and open both PRs: `tailesp32` `feat/esp-idf-port` into its `main` (no handover number, no
   attribution), then CrossInk `feat/tailnet-client` into `main` with the submodule at the head of
   `feat/esp-idf-port`. Ben merges the `tailesp32` PR first. Say in the CrossInk PR body that the submodule must
   move to the merged `tailesp32` commit before merge. CrossInk PR title: "Handover 005 — Tailnet on the X4 Pro
   (direct path)".
