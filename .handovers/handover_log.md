# Handover Log — TRMNL dashboard sleep mode (X4 Pro)

## Destination
When the X4 Pro sleeps, it shows a dashboard image from Ben's self-hosted TRMNL BYOS server. It wakes on a
timer, fetches and redraws without a visible boot or flash, and deep-sleeps in between. Outside the dashboard
hours, on low battery, or after repeated failures, it falls back to another sleep screen. Every knob is in a
new settings tab.

## Intent, verbatim
> i want to make my xteink x4 pro show a dashboard when in standby mode (allow setting dashboard as standby mode)

> i'm inspired by the seeed reterminal

> i'd want the dashboard to refresh on a timer. 1-2-3-5-10-15-30-60 mins?
> ideally wifi connection attempt in the background, re pull, then re-render (ideally without this showing)

> id do deep sleep in between. also allow times when this turns off (22-7 the device sohuld fall back to another sleep mode, for example show custom image)
> iterval, fallback mode, fallback period, all settable in settings
> i'd do BYOS

> H7 -> show a warning banner, and as you say, retry fallback. retry attempts settings
> H8 -> set a battery floor settings
> this all shoukld be a new settings tab imo

## Invariants
- The project invariants in [PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md) hold. Above all: X4 Pro builds only,
  behind a capability flag.
- The device deep-sleeps between refreshes. A failure never keeps it awake.
- A timer wake never shows the boot screen, the home screen, or any UI other than the new frame.
- The last good dashboard image is never replaced by a partial or failed download.
- A button wake behaves exactly as it does today.

## Notes
- Read `AGENTS.md` (via `CLAUDE.md`) and `.claude/CONTEXT.md` first. `pio` is at `~/.platformio/penv/bin/pio`.
- Simulator recipe and flashing: `docs/development/fork-workflow.md`.
- Reference code (read-only, not vendored): `github.com/wolodarskij/cross-trmnl` (MIT, may be ported with
  attribution) and `github.com/DChells/crosspoint-x4-trmnl` (no license file, so ideas only, never code).

## Decisions so far
<!-- index only: one line per decision, linking the handover that holds the detail -->
- Server protocol is TRMNL BYOS (`/api/setup`, `/api/display`). Rejected: a custom protocol, and Seeed
  SenseCraft HMI (closed cloud).
- The server renders HTML to an image. The device only downloads and draws it. Rejected: rendering HTML on the device.
- Flash-free redraw seeds the SSD1677 old-image plane from the saved frame through a new SDK
  `restoreVisibleFrame()`, carried in a `bzatrok/freeink-sdk` fork until Free-Ink merges a PR. See
  [sdk-frame-restore](handover_001_sdk-frame-restore.md). Rejected: shipping v1 with a full refresh per update.
- The local refresh interval setting wins. The server's `refresh_rate` is ignored. A "no change" response skips
  the download and the redraw. See [dashboard-timer-wake](handover_002_dashboard-timer-wake.md). Rejected: the server sets the interval.
- Quiet hours draw the fallback screen once, then sleep until the quiet period ends. Rejected: waking on the
  interval all night.
- Server URL starts as plain `http://` on a network the device can reach. See
  [trmnl-byos-client](handover_003_trmnl-byos-client.md). Rejected for v1: internet-facing HTTPS with certificate checks.
- Server URL and API key are settings. A blank key is fetched through `/api/setup`. The key is never returned
  by the web settings GET. Rejected: typing a token only, and a config JSON on the SD card.
- A failed refresh keeps the last image and draws a warning banner. Retries are a setting. A battery floor is a setting.
- All dashboard settings live in a new settings tab. Rejected: adding them to the Display tab.
- The work is three handovers on separate branches from `main`, in order: SDK, timer wake, BYOS client.
  Rejected: one large branch.
- BYOS server v1 runs at home on the LAN. Hetzner behind a home reverse proxy on the tailnet stays possible
  later, with no firmware change. Rejected: a Tailscale client on the device (microlink adds about 15-20 s per
  cold boot and about 116 KB internal SRAM, and is ESP-IDF only).
- Retry attempts = failed wakes in a row before switching to the fallback screen. Values 1/3/5/10, default 3.
  The banner shows from the first failure. After the switch the device keeps trying at the interval and returns
  on the first success. Rejected: retries inside one wake.
- The fallback screen is the existing Sleep Screen setting. A "Dashboard sleep: On/Off" switch in the new tab
  turns the dashboard on. Rejected: a second fallback-mode picker that duplicates the sleep-mode list.
- Quiet hours: start hour and end hour (0-23) plus Off. Default 22 to 7. Rejected: minute precision.
- Battery floor values: Off/10/15/20/25/30 %, default 15 %.
- New tab "Dashboard", x4-pro builds only: Dashboard sleep, Server URL, API key, Interval, Quiet start,
  Quiet end, Retry attempts, Battery floor, a read-only status line, and a "Refresh now" action.
- Claim gate stays OFF. Most of the proof for this work is on the hardware, which a diff review cannot see.

- [sdk-frame-restore](handover_001_sdk-frame-restore.md) — a new `PanelDriver::restoreVisibleFrame` hook also clears
  the first-paint promotion. Rejected: `seedPreviousFrame` alone, because the first FAST is still promoted to HALF.
- [sdk-frame-restore](handover_001_sdk-frame-restore.md) — the facade clears `_inversionDirty` and leaves
  `_needsGrayClear` alone. Rejected: keeping the inversion flag (second HALF), and dropping an explicit resync.
- [sdk-frame-restore](handover_001_sdk-frame-restore.md) — dual-buffer builds also copy the frame into the
  secondary buffer. Rejected: leaving it white from `begin()`, because the first FAST then writes white into RED.
- [sdk-frame-restore](handover_001_sdk-frame-restore.md) — the SDK fork branch starts at the pinned SHA, and the
  submodule uses HTTPS. Rejected: upstream `main` (unrelated changes), and an SSH URL (breaks clones without keys).
- [sdk-frame-restore](handover_001_sdk-frame-restore.md) — Ben opens the upstream Free-Ink PR. Rejected: the
  executor opens it, because it is an outward-facing action on someone else's repo.

- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — settings live in `CrossPointSettings` under the flag.
  Rejected: a separate store (the tab and web portal would need custom code).
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — the API key is obfuscated on SD and masked in the
  web GET; an empty POST keeps it. Rejected: plain text as cross-trmnl does.
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — all dashboard state is on SD in `state.json`.
  Rejected: RTC memory (lost on power loss, a second store).
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — "Refresh now" sleeps with a 2 s timer.
  Rejected: a foreground Wi-Fi refresh.
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — sleep entry draws the cached image and wakes 2 s
  later to fetch. Rejected: fetching before sleep (reader on screen with Wi-Fi up).
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — HALF refresh every 30 FAST updates, on the first
  draw, and after any fallback. Rejected: FAST forever (ghosting) and HALF always (flash).
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — 60 s backstop plus per-step timeouts; the backstop
  sleeps through `HalPowerManager::startDeepSleep`. Rejected: timeouts alone, and a bare `esp_deep_sleep_start()`
  (skips the power-latch hold and the power-button wake).
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — the local-folder source stays as the offline
  option. Rejected: a test-only stub.
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — the timer is a parameter of `startDeepSleep`.
  Rejected: a global setter.
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — the retry-limit fallback keeps fetching each wake
  and returns on the first success; quiet and battery fallbacks draw once and skip the fetch. Rejected: skipping
  the fetch for every fallback (the device would never recover).
- [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) — the policy has a `Disabled` screen for "feature
  off". Rejected: reusing a fallback value for it.

- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — a new `lib/Trmnl` on `SecureHttpClient`. Rejected:
  `HomeControlHttp` (one extra header only).
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — a pure `TrmnlProtocol` with native tests. Rejected:
  parsing inside the transport (not host-testable).
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — a fixed 2 KB JSON body buffer that fails on overflow.
  Rejected: `getString()` into a heap `String`.
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — the PNG target is exactly the panel size, 1-bit only.
  Rejected: cross-trmnl's square `maxDim` target and its 2-bit variant.
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — battery voltage and model headers are sent. Rejected:
  cross-trmnl's header set.
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — `https://` runs without certificate checks in v1,
  behind `TODO(trmnl-https)`. Rejected: blocking v1 on certificates.
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — no BLE stop before Wi-Fi. Rejected: cross-trmnl's
  `bleinput::stop()` (CrossInk has no BLE stack).
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — `TrmnlSource` (app layer) reads and saves the
  settings; `lib/Trmnl` takes the URL and key as arguments. Rejected: `lib/Trmnl` reading `SETTINGS` (libraries
  cannot include `src/` headers).
- Added by Ben during [trmnl-byos-client](handover_003_trmnl-byos-client.md): a Dashboard **Orientation**
  setting, Portrait by default ("i intend the device to stand portrait for now"). A tall image draws in
  portrait. A wide image on a portrait device is taken as rotated 90° clockwise by the server (Terminus Model
  rotation 90) and drawn back upright. The banner follows the device. Rejected: following the image shape only
  (fails for Terminus rotated images), and landscape only.
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — `Width`/`Height` headers stay 800×480 (the physical
  panel) in both orientations. Rejected: swapping them for portrait, because Terminus stores them on the device
  record and takes rotation from the Model, so a swap risks a double rotation.
- [trmnl-byos-client](handover_003_trmnl-byos-client.md) — a PNG converts to 800×480, or 480×800 when it is tall.
  Rejected: a fixed 800×480 target (crops a portrait PNG).

## Not yet specified
- Reaching the BYOS server away from home: a Tailscale client on the device (microlink) or plain WireGuard.
  Revisit only if the dashboard must work outside the home LAN.
- HTTPS certificate verification for an internet-facing server.

## Out of scope
- ESP32-C3 (X3/X4) and Sticky builds. The project invariant limits fork features to the X4 Pro.
- Building or hosting the BYOS server itself. Ben runs an existing BYOS server (byos_next or Terminus).
- Configuring a home reverse proxy or the tailnet. v1 assumes the server is reachable on the LAN.
- Grayscale or colour dashboards. v1 draws 1-bit images only.
- `/api/log` reporting. Not needed for v1 (see [trmnl-byos-client](handover_003_trmnl-byos-client.md)).

| # | Slug | Goal | Status | Branch | Created | Completed |
|---|------|------|--------|--------|---------|-----------|
| 001 | [sdk-frame-restore](handover_001_sdk-frame-restore.md) | SDK method that seeds the old-image plane after wake | ✅ done | feat/sdk-frame-restore | 2026-10-05 | 2026-10-05 |
| 002 | [dashboard-timer-wake](handover_002_dashboard-timer-wake.md) | Timer wake, silent refresh boot, settings tab, quiet hours, battery floor | ✅ done | feat/dashboard-timer-wake | 2026-10-05 | 2026-10-05 |
| 003 | [trmnl-byos-client](handover_003_trmnl-byos-client.md) | TRMNL BYOS client, provisioning, download, failure banner | ✅ done | feat/trmnl-byos-client | 2026-10-05 | 2026-10-05 |
