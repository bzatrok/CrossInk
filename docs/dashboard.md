---
title: Dashboard Sleep
nav_order: 17
---

# Dashboard Sleep (Xteink X4 Pro)

Dashboard sleep shows a dashboard image while the X4 Pro sleeps. The device
wakes on a timer, refreshes the image without a boot screen or a full flash,
and deep-sleeps again. It is compiled only into the `x4-pro`, `x4-pro-debug`,
and `x4-pro-simulator` environments, gated by `CROSSINK_APP_CAP_DASHBOARD`.
Other devices carry no code for it.

It is not the **Dashboard** sleep screen in Display > Sleep Screen. That one
shows reading stats. This feature has its own settings tab.

## Where it is

**Settings > Dashboard** (the fifth tab). Turn on **Dashboard sleep**, then
sleep the device as usual. The regular **Sleep Screen** setting is the fallback
screen for quiet hours, low battery, and repeated failures.

## Settings

| Row | Values | Default | Meaning |
|---|---|---|---|
| Dashboard sleep | Off / On | Off | Master switch. |
| Server URL | text | empty | Empty = the local folder source (below). The TRMNL BYOS client uses it. |
| API key | text | empty | Stored obfuscated on SD. The web settings page never shows it; an empty POST keeps it. |
| Interval | 1, 2, 3, 5, 10, 15, 30, 60 min | 15 min | Time between timer wakes. |
| Quiet hours | Off / On | On | Show the fallback screen between Quiet start and Quiet end. |
| Quiet start / Quiet end | 0-23 (hour) | 22 / 7 | Local hours; the window wraps past midnight. Equal hours mean no quiet period. |
| Retry attempts | 1, 3, 5, 10 | 3 | Failed wakes in a row before the fallback screen replaces the dashboard. |
| Battery floor | Off, 10-30 % | 15 % | Below it, the fallback screen shows and timer wakes stop until a button wake. |
| Status | read-only | | Last update time, the current failure, or why it is paused. |
| Refresh now | action | | Sleeps with a 2 s timer so the next wake refreshes. Needs Dashboard sleep on. |

Quiet hours need a valid clock (RTC set, year 2026 or later). Without one they
never apply.

## How a refresh works

1. **Sleep entry.** If the policy allows the dashboard and a cached image exists,
   the sleep screen is the cached image (HALF refresh), with the banner if the
   last fetch failed. The device arms a 2 s timer, so the first fetch runs right
   after it sleeps, not with the reader on screen. Otherwise the regular sleep
   screen draws and the timer follows the policy.
2. **Timer wake.** `setup()` takes a headless branch: no frontlight, boot screen,
   Home or reader. It starts a 60 s backstop timer, initialises the panel
   seamlessly, loads `frame.bin` into the framebuffer, and calls
   `display.restoreVisibleFrame()` so the panel knows what is on the glass.
3. **Policy**, in order: off → sleep with no timer; battery below the floor →
   fallback, no timer; inside quiet hours → fallback, wake when they end; failures
   at the retry limit → fallback, keep trying at the interval; otherwise the
   dashboard at the interval. Each fallback kind draws once and then stays.
4. **Fetch.** Wi-Fi connects only when the source needs it (8 s per saved
   network, 15 s total), with an NTP sync when the clock is unset or older than a
   day. A new image goes to `next.bmp` and replaces `current.bmp` only after it
   parses as a bitmap.
5. **Draw.** Updated → redraw. Unchanged → redraw only to remove the banner or a
   fallback screen. Failed → the banner ("Not updated since HH:MM") from the
   first failure, and the fallback screen at the retry limit. The refresh is FAST
   (differential, no flash) when the frame was restored and fewer than 30 FAST
   refreshes ran since the last HALF; otherwise HALF. The first draw after a
   fallback screen is always HALF.
6. **Sleep.** Wi-Fi off, the backstop cancelled, then deep sleep with the next
   timer. If the backstop fires first, it puts the device to sleep with the
   interval timer whatever the main task is doing.

A power-button wake behaves exactly as before.

### Local folder source

With an empty Server URL, each wake copies the next `*.bmp` (name order,
wrapping) from `/dashboard/` on the SD root. One file whose name matches the
last one is "unchanged". An empty folder is a failure ("no images"). Use 800×480
1-bit BMPs; landscape images are drawn full-screen in landscape.

## Storage

`/.crosspoint/dashboard/` holds `current.bmp` (last good image), `next.bmp`
(transient), `frame.bin` (the framebuffer on the glass), and `state.json`. See
`docs/file-formats.md`. Delete the directory to reset the dashboard state. The
settings live in `crossink-settings.json` as `dashboard*` keys.

## Code layout

- `src/dashboard/DashboardPolicy.*` — pure policy, no Arduino includes; native tests.
- `src/dashboard/DashboardState.*` — `state.json` load/save and the status line.
- `src/dashboard/DashboardImageStore.*` — SD paths, validated `next.bmp` swap, `frame.bin`.
- `src/dashboard/DashboardSource.*`, `LocalFolderSource.*` — the source interface
  (`selectDashboardSource()`) and the offline folder source.
- `src/dashboard/DashboardRender.*` — draws the image and banner, picks FAST/HALF, saves `frame.bin`.
- `src/dashboard/DashboardSleep.*` — sleep-entry hooks in `SleepActivity::onEnter` and `enterDeepSleep`.
- `src/dashboard/DashboardWake.*` — the timer-wake path and the 60 s backstop.
- `src/dashboard/DashboardClock.*` — UTC epoch and local time from `HalClock`.
- `src/network/WifiConnector.*` — headless connect to saved networks (ported from cross-trmnl, MIT).
- Hooks: `HalPowerManager::startDeepSleep(gpio, timerWakeSeconds)`, `sleepHardware()`
  and the timer-wake branch in `src/main.cpp`, `SleepActivity::drawBitmapToFramebuffer`.

## Testing

- Native tests: `cmake -S test -B test/build && cmake --build test/build --target DashboardPolicyTest && ctest --test-dir test/build -R Dashboard`.
- Simulator: put BMPs in `fs_/dashboard/`, set `"dashboardEnabled":1` in
  `fs_/.crosspoint/crossink-settings.json`, and run with `CROSSPOINT_SIM_TIMER_WAKE=1`
  and a screenshot at `0` ms (`CROSSPOINT_SIM_SCREENSHOTS="0:/tmp/sim/wake.bmp"`,
  `CROSSPOINT_SIM_INPUT_SCRIPT="1500:QUIT"`). Each run is one timer wake. The
  simulator has no frame restore, so every draw is HALF.
- Hardware: serial lines tagged `DSH`. Each timer wake logs one line with the
  policy, fetch result, refresh mode, next timer, and awake milliseconds.
