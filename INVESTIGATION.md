# E-ink render and refresh latency on the Xteink X4 Pro

Investigation of where time is lost between an input event and the panel finishing
its update. Target: `x4-pro` env (ESP32-S3R8, 800x480 1-bit panel, SPI controller).
Repo state: branch `feat/home-control`, `freeink-sdk` submodule at `918136bf`.

Status at the end of this pass:

| Phase | State |
| --- | --- |
| 1. Read-only mapping | Done. Four detailed sub-reports in `docs/investigation-notes/`. |
| 2. Instrumentation + build | Done: `pio run -e x4-pro-latency` builds (see "Measurement"). **No hardware was connected, so nothing was measured.** Every ms figure below is arithmetic from source and is labelled as such. |
| 3. Report | This file. |

Rule used throughout: a claim carries a `path:line` or is marked **UNVERIFIED**.
Lines in `freeink-sdk/` are shortened: `FID/` = `freeink-sdk/libs/display/FreeInkDisplay/`,
`BC.h` = `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`.

---

## 1. Build facts (verified)

| Item | Value | Source |
| --- | --- | --- |
| Platform | pioarduino platform-espressif32 55.03.37 | `platformio.ini:14` |
| Arduino core | 3.3.7 | `~/.platformio/packages/framework-arduinoespressif32/package.json` |
| ESP-IDF | release/v5.5 (87912cd291), prebuilt libs | `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/versions.txt` |
| SDK config used | stock prebuilt `esp32s3/dio_opi`; the env has no `custom_sdkconfig`. The repo-root `sdkconfig.defaults` is a hash-tagged leftover from a `firmware_tuned` env and does not apply (it is gitignored, `.gitignore:51`). | `platformio.ini:411-450`; pioarduino `builder/frameworks/arduino.py:571,616-660` |
| CPU | 240 MHz; drops to 80 MHz after 3000 ms without input (`BOARD_HAS_PSRAM` comes from the board json) | `dio_opi/include/sdkconfig.h:1078`; `lib/hal/HalPowerManager.h:38-43`; `src/main.cpp:1974-1977` |
| PSRAM | Octal, 80 MHz, `malloc` above 4096 B goes to PSRAM | `dio_opi/include/sdkconfig.h:1062,1067,1072-1073` |
| Flash | DIO, 80 MHz | `dio_opi/include/sdkconfig.h:461,463`; `platformio.ini:80,423` |
| Optimisation | `-Os` (`CONFIG_COMPILER_OPTIMIZATION_SIZE`) | `dio_opi/include/sdkconfig.h:766`; `esp32s3/pioarduino-build.py:109` |
| FreeRTOS | 1000 Hz tick, time slicing on, 2 cores | `dio_opi/include/sdkconfig.h:1196,1233` |
| Arduino `loopTask` | core 1, priority 1, 16 KB stack (FreeInkUI weak override of the 8 KB default) | `sdkconfig.h:483-484`; `freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:13` |
| Caches | I-cache 16 KB, D-cache 32 KB, 32 B lines | `dio_opi/include/sdkconfig.h:1080,1086,1090` |
| Framebuffer | 48000 B, single buffer, `heap_caps_malloc(MALLOC_CAP_SPIRAM)` | `platformio.ini:35,442`; `FID/src/FreeInkDisplay.cpp:378-390` |
| `SPI_MASTER_*_IN_IRAM` | not set; irrelevant because the panel uses Arduino `SPIClass`, not the IDF `spi_master` driver | `esp32s3/sdkconfig:1927-1928`; `FID/src/bus/EpdBus.cpp:105,157-229` |

## 2. Pipeline diagram

Default settings assumed: anti-aliased text on (`textAntiAliasing = 1`,
`src/CrossPointSettings.h:496`), refresh cadence 15 pages (`:582`), side key Down =
page turn on short press (`:550`), fading fix off. Controller assumed **UC8279**
(see "Unverified"); the SSD1677 and UC8179 branches differ as noted.

```
 loopTask (core 1, prio 1)                 ActivityManagerRender (core 1, prio 1)
 ───────────────────────────               ─────────────────────────────────────
 delay(10)  [50 ms + 80 MHz after 3 s idle]
   │
 mappedInputManager.update()               
   ├─ InputManager::update()  digitalRead GPIO0/GPIO7 (polled, no ISR)
   │    debounce: change commits >5 ms later
   ├─ HalGPIO re-poll: delay(6) when debounce pending
   └─ GT911 poll over I2C every loop (no IRQ, no rate limit)
   │
 ActivityManager::loop()
   └─ EpubReaderActivity::loop()
        └─ SideButtonShortcuts: fires on RELEASE (or 700 ms hold = long action)
             └─ requestManualPageTurn()   [200 ms min gap; queue of 5]
                  └─ pageTurn() → requestUpdate() (flag)
 end of loop: xTaskNotify(renderTask) ─────────────▶ ulTaskNotifyTake
                                                      RenderLock (held to the end)
                                                      HalPowerManager::Lock (240 MHz)
                                                      EpubReaderActivity::render()
                                                        clearScreen (48 KB memset, PSRAM)
                                                        Section::loadPage (SD read + deserialize)
                                                        scan pass (glyph prewarm; FreeType only on cache miss)
                                                        B/W rasterise (PSRAM framebuffer)
                                                        status bar x2 (SD metadata reads)
                                                      ── panel, UC8279 AA page ──
                                                      displayGrayscaleBase(FAST)
                                                        → transitionGrayscaleBase:
                                                            DTM2 ← frame      60 KB SPI
                                                            precondition DRF  (waveform #1, BUSY poll 1 ms)
                                                            DTM1 ← frame      60 KB SPI
                                                      storeBwBuffer (6 x 8 KB copies)
                                                      rasterise LSB plane → copyGrayscaleLsb
                                                            OR into _grayBase (48 KB loop)
                                                            DTM1 ← ~plane     60 KB SPI
                                                      rasterise MSB plane → copyGrayscaleMsb
                                                            popcount (48 KB) + XOR stream
                                                            DTM2 ← plane      60 KB SPI
                                                      displayGrayBuffer → displayGray:
                                                            5 LUTs, PON?, DRF (waveform #2, BUSY poll 1 ms)
                                                            DTM1 ← base, DTM2 ← base   2 x 60 KB SPI
                                                      restoreBwBuffer
                                                      progress save (debounced), next-chapter index
                                                      LatencyTrace::dump()   [trace build only]
```

Per AA text page on UC8279 that is **6 plane uploads (~360 KB) and 2 waveforms**.
A plain B/W page (anti-aliasing off, or a monochrome font) is 2 uploads and 1 waveform.
Sources: `src/activities/reader/EpubReaderActivity.cpp:7302-7305,7464,7480-7509`;
`FID/src/driver/Uc8279X4Driver.cpp:723-761,768-809,595-648,650-721`.

What the user sees: the new B/W page appears when waveform #1 completes. The
anti-aliased edges appear when waveform #2 completes. Everything after that (the
2 x 60 KB restore) is invisible but holds the render task, so it delays the *next*
turn.

### Input path facts (`docs/investigation-notes/input-pipeline.md`)

- Buttons: GPIO0/GPIO7 active-low, polled, `INPUT_PULLUP`, no interrupt
  (`BC.h:1664`; `freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:119-136,338-367,526-566`).
- Debounce: `DEBOUNCE_DELAY = 5` (`InputManager.h:454`), committed on a later poll.
  `HalGPIO::update()` re-polls after `delay(6)` when a debounce is pending
  (`lib/hal/HalGPIO.cpp:32,186-195`).
- Touch: GT911 over I2C at 400 kHz, polled every loop, no IRQ, tap/swipe classified
  on the first zero-contact frame (`InputManager.cpp:1264-1309,1922-1938,2243-2373`).
  I2C bus is shared with the RTC and the battery gauge; Arduino `Wire` holds a
  `portMAX_DELAY` mutex (`BC.h:1676-1678`; Wire.cpp:419-427).
- Page turn fires on release (`src/activities/reader/SideButtonShortcuts.h:53-85`).
- Loop period: `inputPollDelayMs()` = 10 (`src/activities/Activity.h:59`); after
  `IDLE_POWER_SAVING_MS = 3000` the loop uses `delay(50)` and 80 MHz
  (`src/main.cpp:1957-1981`; `lib/hal/HalPowerManager.h:38-43`).
- Loop → render: flag + `xTaskNotify` at the end of `ActivityManager::loop`
  (`src/activities/ActivityManager.cpp:726-731`); render task blocks in
  `ulTaskNotifyTake` (`:478`). Same core and priority as the loop, so the hop costs
  at most one tick after the loop blocks in its `delay`.
- The SDK's own 15 ms input task (`InputManager::beginAsync`) is never started
  (zero callers in `src/`, `lib/`, `include/`).

### Display path facts (`docs/investigation-notes/display-pipeline.md`)

- Driver chosen at boot by probing the controller VER register: SSD1677 default,
  else UC8179 or UC8279 (`src/main.cpp:1187`; `XteinkDetect.cpp:384-387,499-528`;
  `FID/src/FreeInkDisplay.cpp:146-170`). All three are linked (`BC.h:120-155`).
- SPI clock 10 MHz for every Xteink board (`BC.h:882`, used `:1644`), overriding the
  UC8279 driver's own 16 MHz default, whose comment rates the part at 20 MHz
  (`FID/src/driver/Uc8279X4Driver.cpp:244-247`). The SSD1677 driver default is 40 MHz
  (`Ssd1677Driver.cpp:186-188`).
- Transport: Arduino `SPI.writeBytes` → `spiWriteNL`: 64-byte FIFO loads, CPU spins on
  `cmd.usr`, no DMA (`esp32-hal-spi.c:1456-1493`). Mode 0, MSB first (`EpdBus.cpp:85`).
- UC8279 plane upload: 600 gates x 100 B = 60000 B per plane, each visible row copied
  byte-by-byte into a 128 B stack buffer first (`Uc8279X4Driver.cpp:321-355`).
- Fast refresh = DU waveform (TSSET 0x5A, CDI 0xD7, PTIN + full-screen PTL window,
  OTP LUT) (`:459-510`). After every refresh the frame is re-sent to DTM1 so the next
  DU has a baseline (`:512-523`).
- BUSY wait on UC parts: `delay(1)` then 1 ms polling, no timeout, no ISR
  (`EpdBus.cpp:278-294,328-331`; `Uc8279X4Driver.h:69`). The SSD1677 path has an ISR
  semaphore instead (`EpdBus.cpp:71-78,359-403`). The DRF-start spin is up to 50 ms of
  `delay(1)` (`Uc8279X4Driver.cpp:505-509`).
- All reader refreshes on UC parts are blocking: async needs `asyncBase`, which UC
  drivers report false (`Uc8279X4Driver.cpp:562-574`; reader gate
  `EpubReaderActivity.cpp:7410-7412`).
- No partial-window updates anywhere in the app: `GfxRenderer::displayWindow` is
  commented out (`lib/GfxRenderer/GfxRenderer.h:228-229`); only the SSD1677 driver
  implements a window (`Ssd1677Driver.cpp:554-599`).
- No host-side frame diffing; the controller diffs DTM1 against DTM2.

### Render path facts (`docs/investigation-notes/render-pipeline.md`)

- Drawing writes straight into the SDK's PSRAM framebuffer; no back buffer
  (`FID/src/FreeInkDisplay.cpp:600-603`; `lib/GfxRenderer/GfxRenderer.cpp:239-250,926-966`).
- Rotation is per-pixel at draw time (`GfxRenderer.cpp:378-407`).
- Glyphs: FreeType on device with a direct-mapped PSRAM cache; rasterisation only on a
  miss (`lib/ScalableFont/HalScalableFont.cpp:157-170,228-262`).
- Each AA page rasterises the text three times (B/W, LSB, MSB) plus a scan pass
  (`EpubReaderActivity.cpp:7268-7274,7340-7348,7488-7497`).

### Tasks (`docs/investigation-notes/task-inventory.md`)

| Task | Core | Prio | Stack | Touches panel |
| --- | --- | --- | --- | --- |
| `loopTask` (input, policy) | 1 | 1 | 16 KB | yes, under RenderLock on sleep/manual-refresh paths |
| `ActivityManagerRender` | 1 | 1 | 24 KB | yes, main path (`ActivityManager.cpp:456-466`; `src/main.cpp:376`) |
| `DictLookup` | any | 1 | 4 KB static | no |
| `esp_timer`, `tcpip`, `wifi`, `sys_evt`, `mdns`, `async_udp` | 0 | 18-22 | IDF defaults | no |
| `arduino_events` (Wi-Fi up only) | 1 | 19 | 4 KB | no |
| `usbd` (USB Drive only) | any | 24 | 4 KB | no |

Shared locks on the hot path: `renderingMutex` (RenderLock; loop uses Try mode),
`HalPowerManager::modeMutex` (CPU clock switch under it), Arduino `Wire` mutex,
`HalSpiBus` recursive mutex (display + storage).

## 3. Time per stage

**Measured 2026-10-06** on Ben's X4 Pro with the trace build (raw log:
`docs/investigation-notes/x4-pro-trace-2026-10-06.log`). The unit probes as UC8279
(SDK refresh tag `8279x4_DRF`). Column "Measured" is from `LAT:` blocks; the rest of
the table is the pre-measurement estimate kept for comparison.

| Stage | Measured |
| --- | --- |
| Framebuffer render (`render_start` → `display_call`) | 16 ms (boot/simple screens) to 125 ms (Home with covers) |
| DTM2 upload (`driver_start` → `waveform_wait`) | 66-73 ms (estimate was 48) |
| Waveform #1, normal refresh (`busy_wait` → `busy_done`) | **483 ms, identical on every render** |
| Waveform, full refresh (dashboard draw, `8279x4_DRF`) | 1329 ms, plus `8279x4_POF` 76 ms |
| DTM1 resync after busy (`busy_done` → `driver_return`) | 63-69 ms (estimate was 48) |
| **Whole B/W render, tap to panel done** | **~650-800 ms**, of which 483 ms is the panel |
| Internal heap at runtime | 195 KB free, 147 KB largest block (`[MEM] Periodic`) |
| Main loop during a render | not blocked (`main.cpp` Try-lock); edge → `render_start` is ~1 ms when idle, 300-1150 ms when a render is already running (queued) |

No AA (grayscale) page turn was captured yet; all blocks were 2-plane renders.

Consequence for the ranking in §4: #1 (SPI clock) saves at most ~65 ms of ~650;
#4 (shorter waveform) is the only item that can halve a turn; #14 fits in heap.

### Estimates before measurement

Nothing was measured when this table was written. Column "Source" says where the number comes from. Wire time
= bytes x 8 / 10 MHz, excluding FIFO refill gaps (938 refills per plane, each a few
register writes; not quantified).

| Stage | Estimate | Source |
| --- | --- | --- |
| Physical press → press edge seen | 0-10 ms poll + 6 ms re-poll (reading idle >3 s: 0-50 ms + 80→240 MHz switch, switch time UNVERIFIED) | `main.cpp:1965-1980`, `HalGPIO.cpp:192` |
| Press edge → release edge | user hold time; firmware adds the same poll + 6 ms again | `SideButtonShortcuts.h:71-77` |
| Release → render task running | < 2 ms (flag, notify, loop enters `delay`) | `ActivityManager.cpp:726-731,478` |
| `clearScreen` 48 KB memset in PSRAM | ~1 ms (UNVERIFIED) | `FreeInkDisplay.cpp:240` |
| Page load + scan + B/W rasterise + status bar | **unknown; measure** (SD reads + cache-hit glyph blits) | render report §4 |
| DTM2 upload (60000 B) | 48 ms wire | `Uc8279X4Driver.cpp:321-355` |
| Refresh setup + DRF-start poll | 1-50 ms (`delay(1)` spin until BUSY drops) | `:505-509` |
| Waveform #1 (precondition / DU) | **unknown; measure.** The only in-source number is a 443 ms DRF on a no-image unit (`:482`), not representative | — |
| BUSY poll granularity | +0-1 ms | `EpdBus.cpp:284-294` |
| DTM1 resync upload | 48 ms wire (invisible to the user) | `:521`, `:752` |
| LSB plane: rasterise + 48 KB OR + 60 KB upload | rasterise unknown + ~1 ms + 48 ms | `:595-612` |
| MSB plane: rasterise + popcount + 60 KB upload | rasterise unknown + ~1 ms + 48 ms | `:623-648` |
| Gray LUT upload + DRF | 5 x 49 B + commands, negligible | `:657-685` |
| Waveform #2 (gray) | **unknown; measure** | `:686` |
| Base restore 2 x 60 KB | 96 ms wire (invisible) | `:702-704` |
| **SPI wire total, AA page** | **~288 ms** (6 planes) | arithmetic |
| **SPI wire total, plain B/W page** | **~96 ms** (2 planes) | arithmetic |
| Rapid consecutive turns | +200 ms minimum spacing | `EpubReaderActivity.cpp:110,5849` |

With fading fix on, each refresh adds POF + wait and PON + wait (`:387-392,525-529`).

## 4. Ranked changes

Ranking is by expected saving per page turn on the assumed UC8279 unit, weighted by
risk. "HW test" = must be validated on the device (visual ghosting or timing).
Savings are wire-time arithmetic unless marked; waveform-bound items need the trace
build first.

| # | Change | Files | Mechanism | Expected saving | Risk | HW test |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | **Raise display SPI clock 10 → 20 MHz** | `BC.h:882` (or a per-board `displaySpiHz` for X4 Pro at `BC.h:1644`) | Halves every plane upload. UC8279 driver comment rates the part at 20 MHz (`Uc8279X4Driver.cpp:245`); SSD1677 default was 40 MHz. SCLK/MOSI on GPIO12/11; verified 2026-10-06: GPIO12 = `SPI2_IOMUX_PIN_NUM_CLK`, GPIO11 = `SPI2_IOMUX_PIN_NUM_MOSI` (IDF `soc/spi_pins.h`), but the Arduino HAL attaches SCK via `pinMatrixOutAttach` (`esp32-hal-spi.c:278`, core 3.3.7), i.e. the GPIO matrix, so the IOMUX ceiling does not apply as-is. The link is write-only (no MISO), which is the case the matrix penalises least | ~48 ms per B/W page, ~144 ms per AA page | Signal integrity on the flex: corrupt rows, ghosting. Trivial to revert | yes |
| 2 | **Trigger the page turn on press, not release, when no long action is bound** | `src/activities/reader/SideButtonShortcuts.h:53-85`, default `sideButtonDownLong = SIDE_NEXT_CHAPTER` (`CrossPointSettings.h:551`) | Removes the user's hold time plus one poll + 6 ms debounce round. Needs the long action moved to "press-and-hold after the turn" or disabled | tens of ms to >100 ms perceived (hold time UNVERIFIED) | UX change; chapter-skip long press must stay reachable | yes (behaviour) |
| 3 | **Do not drop to 50 ms polling / 80 MHz while in the reader** | `src/main.cpp:1974-1977`, `lib/hal/HalPowerManager.h:43` | A page is read for >3 s, so almost every turn hits the slow path: up to 40 ms extra poll latency plus the clock switch before the loop body runs | 0-40 ms + switch time | Battery: 10 ms polling at 240 MHz while reading. Mitigate with a longer threshold (30 s) or by using GPIO wake (`InputManager::beginAsync` exists unused, `InputManager.cpp:225-273`) | yes (power) |
| 4 | **Faster DU waveform via external LUT** ("A2-style") | `Uc8279X4Driver.cpp:459-510` (OTP DU today), LUT tables `:50-77`, `setCustomLUT` plumbing `FreeInkDisplay.cpp:1054` | The fast path already uses the controller's OTP DU. A shorter REG=1 table (fewer frames) cuts waveform #1; the AA path already runs external tables | Unknown until waveform #1 is measured; potentially the largest single item | **Incorrect LUTs can damage the panel (DC imbalance, overdrive) and cause permanent ghosting. Do not ship untested tables.** | yes, carefully |
| 5 | **Skip the 2-plane base restore after the gray pass when the next op rewrites both planes** | `Uc8279X4Driver.cpp:702-707` vs `:748-752` | `transitionGrayscaleBase` re-streams DTM2 and DTM1 on the next page anyway; the restore is redundant unless a non-AA display call intervenes. Keep a "planes dirty" flag and restore lazily | 96 ms of render-task time per AA page (not visible, but it gates the next turn) | Controller RAM state must be tracked exactly; wrong baseline = ghosting | yes |
| 6 | **Stop streaming the 120 hidden gates** | `Uc8279X4Driver.cpp:229-230,328-330` (`tresHeight 600`, `gateOffset 120`, GSST = 0 at `:253-284`) | Program GSST (gate start) to 120 and TRES to 480 so a plane is 48000 B, not 60000 B | 20% of every upload: ~10 ms per plane, ~58 ms per AA page | Panel timing is copied from stock firmware; may shift the image or break the partial window | yes |
| 7 | **Full-refresh cadence** | `CrossPointSettings.h:582` default `REFRESH_15`; `ReaderUtils.h:238-255`; NEVER allowed on frontlit boards `CrossPointSettings.cpp:1262-1264` | Every 15th page runs HALF: 3 planes (144 ms) + a GC waveform (longer; UNVERIFIED) + `preconditionGrayscale`. Default 30 or NEVER on X4 Pro amortises it; the AA path's own re-drive already scrubs edges (`:443-447,716`) | ~(144 ms + GC time)/15 per page on average | Ghost build-up; NEVER needs a manual refresh habit | yes (visual) |
| 8 | **Overlap rasterisation with SPI via DMA (IDF `spi_master`)** | `FID/src/bus/EpdBus.cpp` (whole transport), `Uc8279X4Driver.cpp:321-385` | Today the CPU spins through 48 ms per plane. IDF 5.5 docs (fetched): DMA buffers must be in internal DMA-capable memory, 32-bit aligned, `max_transfer_sz` 4092 by default. So: 2 x 4 KB internal bounce buffers, queue transactions, rasterise the next plane while the previous streams. The FSPI host is exclusive to the panel on X4 Pro (SD is SDMMC) | Up to min(rasterise, 48 ms) per overlapped plane, i.e. up to ~96 ms per AA page if rasterisation is ≥48 ms; 0 if rasterisation is fast. **Measure first** | Medium-high: new transport, DMA cache coherence with PSRAM sources, Arduino `SPI` must no longer own the host | yes |
| 9 | **Dirty-rectangle partial updates** | `FID/src/FreeInkDisplay.cpp:806-825` (window API exists), UC drivers lack `displayWindow` (only `Ssd1677Driver.cpp:554-599`), `GfxRenderer.h:228-229` | UC8279 fast path already runs PTIN with a full-screen PTL window (`:480-497`); a real window limits both the upload and the waveform to the changed rows | Proportional to unchanged rows. **0 for a text page turn** (every row changes). Large for status bar, progress, popups, menus | Ghosting at window edges; per-driver work | yes |
| 10 | **Back-buffer diffing** | new 48 KB PSRAM shadow + compare pass | Enabler for #9: compute the changed-row span after rasterisation. ~1 ms compare | 0 alone; makes #9 automatic | +48 KB PSRAM | no |
| 11 | **Render all three planes in one traversal** | `lib/GfxRenderer/GfxRenderer.cpp:926-966` (`setRenderMode`), `GlyphBitmap.h:94-153`, `EpubReaderActivity.cpp:7488-7497` | Glyph blit writes B/W, LSB and MSB bits in one pass to three buffers; saves two full text traversals and two `clearScreen` passes | 2 x (rasterise time); **measure first** | Renderer change; +96 KB PSRAM for two planes | no (correctness via simulator) |
| 12 | **Reduce input poll period in the reader** | `EpubReaderActivity.h` override of `inputPollDelayMs()` (`Activity.h:59`) | 10 → 5 ms halves the average edge latency | ~2.5 ms avg, 5 ms max per edge | Power | no |
| 13 | **Move the render task to core 0** | `ActivityManager.cpp:456-460` | Loop and render stop time-slicing in 1 ms quanta during rasterisation | <5 ms; both tasks block most of the time | Core 0 hosts Wi-Fi/tcpip/esp_timer; the task WDT watches only IDLE0, so a CPU-bound render >5 s would panic (this is why the code pins to core 1) | yes |
| 14 | **Framebuffer in internal SRAM** | `platformio.ini:442` (`FREEINK_FB_PSRAM`) is not enough: `malloc` >4096 B lands in PSRAM anyway (`sdkconfig.h:1073`); needs `MALLOC_CAP_INTERNAL` in `FreeInkDisplay.cpp:378-390` | Every full pass (memset, 3 rasterisations, 6 plane copies, 3 x 48 KB `_grayBase` loops) streams 48 KB through the 32 KB D-cache | A few ms per AA page (UNVERIFIED; PSRAM at 80 MHz octal is ~1 ms per 48 KB pass in theory) | Internal heap: static use is 108 KB of 320 KB (build output), runtime free heap UNVERIFIED | yes |

Not recommended as latency work: `GrayscaleMode::Direct` (one waveform for base + gray)
exists in the driver but forces `_needFullClear` on the following page
(`Uc8279X4Driver.cpp:717`), so it would trade one waveform for a full flash.

## 4a. Reported symptom: presses that need repeating

Ben reports that the Home key, the power button and both side keys sometimes need
a second press. All four are sampled by the same loop pass (`src/main.cpp:1698` →
`HalGPIO::update` → `InputManager::update`; Home key via the GT911 I2C poll inside
the same call), so a shared cause is expected. Candidates verified in source, not yet
confirmed on hardware:

1. **50 ms idle poll.** After 3000 ms without input the loop samples every 50 ms at
   80 MHz (`src/main.cpp:1974-1977`). A press must be seen on one pass and still be
   held 6 ms later (`lib/hal/HalGPIO.cpp:186-195`). A tap shorter than the gap is never
   seen. Reading a page takes >3 s, so the first press after reading always runs in
   this mode. Prime suspect.
2. **Loop stalls.** Blocking RenderLock at chapter boundaries and activity push/pop,
   the `HalSpiBus` mutex during a blocking refresh, the `Wire` mutex (GT911 shares I2C
   with RTC and gauge), idle font prewarm, and the section-build tick all stop the
   loop from sampling. Press + release inside one stall = no edge. The SDK documents
   this (`InputManager.h:217-229`); its queued input task `beginAsync` is never started.
3. **Not a cause:** presses during a refresh are queued (up to 5), and turns within
   200 ms are delayed, not dropped.

Side effects that make it feel worse: Home single-tap waits out a 300 ms double-tap
window (`src/main.cpp:159,921`), so a repeated tap can become a double tap; power
and side keys act on release only.

Confirmation with the trace build: a lost press shows as a turn with no preceding
`input_edge` within the expected gap; a repeat shows two edges before one turn.
Fix candidates: ranked item #3 (keep 10 ms polling in the reader), or start the SDK
input task so presses are captured as events independent of the loop.

## 4b. Measured: the two-tap symptom (2026-10-06)

Ben's test on the Cover Grid home: a brisk tap on Settings opened it first time; a
normal tap had needed two. Root cause, verified in source, consistent with the test:

- `src/components/UiAppHelpers.h:211 touchSnapshotFrom()` asks `wasScreenLongPress()`
  first. The SDK fires a long press after a stationary hold of `TOUCH_LONG_PRESS_MS = 500`
  (`freeink-sdk/.../InputManager.h:470`), and `MappedInputManager::wasScreenLongPress()`
  (`src/MappedInputManager.cpp:353-367`) then calls `suppressTouchContact()`, so the lift
  is no longer a tap.
- The snapshot is routed as `InputLongPress`; a plain tile only accepts `InputTouch` and
  `findTouch()` (`FreeInkUICore.h`) has no fallback for long press, so nothing fires.
- The touch-down `StateActive` highlight repaint (483 ms refresh) is the "select" the user
  sees, and e-ink's delay invites holding the finger past 500 ms.
- Affects `CoverGridHomeUi` and every screen using `touchSnapshotFrom` (~30 list and
  settings screens). The Lyra and carousel home paths do not use it.
- Fix options (not applied): skip the long-press query on screens where no element
  accepts `InputLongPress`; or raise the threshold there; or make the SDK not suppress
  the contact when the long press went unconsumed.

Still open after the test, needs the timestamped event lines (see §6):
- Home key "needed two taps": single tap is deferred by the 300 ms double-tap window
  (`main.cpp X4PRO_HOME_KEY_DOUBLE_TAP_MS`), so first visible change is ~300 ms + a
  ~700 ms render later; a second tap inside 300 ms becomes the double-tap action
  (default: toggle frontlight). Unconfirmed which happened.
- One Library tile tap "not registered": no render followed. Could be the 500 ms
  long-press path again, or a tap landing during the previous refresh.

## 4c. Measured: which taps are quick and which are slow (2026-10-06, event-line build)

Log: `docs/investigation-notes/x4-pro-trace-2026-10-06-eventlines.log`. Every tap in this
run registered (`edge tDown/tUp` lines). All refreshes are `8279x4_DRF` at 483 ms, so
one refresh costs ~650-700 ms end to end and the differences come from *how many*
refreshes a tap triggers and whether the render task was already busy.

| Tap | Refreshes | Input → panel | Why |
| --- | --- | --- | --- |
| Settings list item | 1 | 660-1000 ms | One render per tap. Settings does not use FreeInkUI, so no touch-down repaint. |
| Library tile (Cover Grid home) | 1 (+1 queued before it) | 995 ms | Render task was still finishing Home's second refresh, so render_start came 358 ms after the lift. |
| Home key (from Library or reader) | 2 | 1077 ms first paint, 1735 ms final | 300 ms double-tap deferral, then `HomeActivity::render()` always requests a second render (`firstRenderDone` → `requestUpdate()`, `HomeActivity.cpp:2087,2145,2205,2271`). The second refresh is identical when thumbnails are cached and blocks the next tap. |
| Cover Grid tile (Settings, book) | 2 | 1484 ms to Settings | FreeInkUI sets the tile active on touch-down (`FreeInkUICore.h:1313`), `app.invalidated()` → `requestUpdate()` (`HomeActivity.cpp:1397`), so a highlight-only refresh (711 ms) runs *before* the action's render can start. |
| Open book | 3 refreshes + layout | 6050 ms | Section cache was missing (`Failed to open .../sections/0.bin`), layout took 3.2 s and then failed: `[EHP] Couldn't allocate memory for buffer`, `[SCT] Failed to parse XML and build pages`. Separate bug, not a latency item; internal heap was 188 KB free / 147 KB max block at the time. |

What this means for the "loading state" question: FreeInkUI's tap flash is designed to
ride in the same refresh as the result (`FreeInkApp.h:905-916`), but navigating handlers
clear it, and on the Cover Grid the touch-down highlight already costs a full refresh of
its own. A separate loading frame would add another ~650 ms on this panel. The lever is
fewer refreshes per tap, not more feedback frames:

1. Cover Grid: do not repaint on touch-down (or paint the highlight only in the result
   frame as the SDK intends). Saves one refresh (~700 ms) on every tile tap.
2. Home: skip the unconditional second render when nothing changed (thumbnails cached,
   `recentsLoaded`). Saves one refresh after every Home key press and un-blocks the next tap.
3. Long-press suppression fix from §4b (lost taps).
4. Home key: the 300 ms deferral is only needed when a double-tap action is configured.
5. Waveform (#4) shortens every remaining refresh.

## 4d. Measured: Home key presses that need repeating never reach the firmware (2026-10-06)

Third capture (`scripts/capture_latency_trace.py`, event-line build with Home render
requesters tagged). Ben: "Home needed 2 taps from Settings, 3 from Library". The log
shows exactly one Home key press edge per episode (`hkP=1` at @27571 and @40999),
four and six seconds after the last touch. The repeated presses produced no input
edge at all, so nothing downstream (deferral, double-tap, render) was involved.

How the key is read (`freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp
pollGt911`): the GT911 status register 0x814E is polled once per main-loop iteration;
the key press and release edges are taken from bit 0x10 only on a fresh frame (bit
0x80), and 0x814E is cleared after each read. The X4 Pro wires the GT911 INT line to
GPIO10 (`BoardConfig.h:1680-1699`, irq=10) but the GT911 path never uses it. The loop
polls every 10 ms while active and every 50 ms with the CPU at 80 MHz after 3 s idle
(`main.cpp` tail, `HalPowerManager::IDLE_POWER_SAVING_MS = 3000`, `LOW_POWER_FREQ = 80`).
The two presses that did register show 14 ms and 38 ms between press and release
frames, i.e. the key contact is short. Working hypothesis (not yet proven): a short key
tap during the 50 ms idle cadence is overwritten by the lift frame before the host
reads it, so neither edge is seen. Both lost episodes happened after >3 s idle; the
Home key presses in the second capture that worked first time came ~1 s after activity.

Test that settles it: keep the 10 ms poll while idle (plan item #3) and repeat the
Home key after 5 s idle. If still lost, latch frames from the INT edge on GPIO10 instead.

Also from this capture: the home screen is not the Cover Grid theme on Ben's device
(the three tagged `home:` requesters never printed), so the three renders per entry
come from another theme branch in `HomeActivity::render()` (Lyra carousel
`preRenderCarouselFrames` → `requestUpdate`, or the default branch's
`carouselWarmupPending`). Which one is open until the theme is known.

**Correction (same day, capture 5, idle poll kept at 10 ms):** the idle-poll hypothesis is
wrong and the change was reverted. With the 10 ms poll the counts were unchanged (2, 3, 1,
2 presses). The log shows the "lost" presses as *screen touches* (`tDown`/`tUp`, 100-160 ms
contact) with no key bit, then the press that worked as `hkP`/`hkTap`; one press reported
both at once. So the GT911 reports a Home key press as a touch contact when the finger
also covers the glass above the key. Next capture prints the contact coordinates; if they
sit in a fixed strip at the panel's bottom edge, the fix is to route that strip to the
Home key events (the SDK already does this for the GSLX680 sentinel, `InputManager.cpp:1852`).

Also attributed with the requester addresses (`addr2line` on `firmware.elf`): on Lyra
Extended, Home renders twice per entry, not three times: `HomeActivity::onEnter()`
(`HomeActivity.cpp:929`) and the default branch's `firstRenderDone` re-request
(`HomeActivity.cpp:2294`). The render that followed a tile touch-down came from
`HomeActivity::loop()` (`HomeActivity.cpp:1950`): `wasCoverTouchedDown` /
`wasItemTouchedDown` move the selector and repaint. Both are fixed on
`perf/x4-pro-fewer-refreshes` (second render dropped, touch-down no longer repaints).

**Verified (capture 8, same day):** with the GT911 path suppressing the screen contact while
the key is down (`InputManager.cpp pollGt911`, end), four Home key presses after 5 s pauses
all registered first time ("flawless"). The one press that also produced a contact landed at
normalized (0.994, 0.459): the panel edge next to the key, suppressed. Home key → Home is
now one render, 1.10-1.18 s from the key release (300 ms deferral + ~700 ms refresh + ~100 ms
render). Settings and Library tiles open in one refresh, 650-700 ms from the lift, with no
touch-down repaint. Lost-press case with no key bit at all did not recur in this run; if it
does, route contacts at x > 0.98 to the key.

## 5. Suggested order

1. Flash the trace build and fill in the "unknown" rows (render time, waveform #1,
   waveform #2). That decides whether #4 and #8 are worth doing at all.
2. #1 (SPI clock) and #3 (no idle slowdown in reader): one-line changes, large wins,
   easy A/B.
3. #2 (turn on press) as a setting, default off until tried.
4. #5 and #6 together: both are UC8279 driver RAM-state changes and need one
   ghosting validation session.
5. #7 as a default change once #5/#6 are stable.

## 6. Measurement (how to get real numbers)

Build: `~/.platformio/penv/bin/pio run -e x4-pro-latency` → `.pio/build/x4-pro-latency/firmware-x4-pro.bin`.
Flash via the local Inky flow in `docs/development/fork-workflow.md:97-129`, or
`pio run -e x4-pro-latency -t upload` over USB. Attach serial at 115200. Turn pages.

After every render the device prints:

```
LAT: ---- trace (N marks, t0 = input_edge) ----
LAT:      0.00 ms  (+    0.00)  input_edge
LAT:     12.34 ms  (+   12.34)  page_turn
LAT:     ...                    render_start
LAT:     ...                    display_call / driver_start / busy_wait / busy_done ...
LAT:     ...                    waveform_wait / busy_released   (refresh completion)
LAT:     ...                    driver_return
LAT:     ...                    render_done
LAT: total X ms
```

`input_edge` is the last button/touch edge seen by the loop before the turn
(the release edge for a short press). `display_call` marks the framebuffer complete.
`driver_start` is the start of SPI streaming. On UC parts the DRF wait shows as a
`busy_wait`/`busy_done` pair nested in `waveform_wait`/`busy_released`. The SDK's
own `Wait complete: <tag> (N ms)` lines (`EpdBus.cpp:319-321`) print alongside.

Instrumentation files (all behind `CROSSINK_LATENCY_TRACE` / `FREEINK_LATENCY_TRACE`,
no-ops otherwise; the default `x4-pro` env was rebuilt to confirm):

- `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkLatencyTrace.h` (new): mark ids, weak hook, RAII scopes.
- `freeink-sdk/.../src/bus/EpdBus.cpp`: scopes in `waitBusy` and `waitRefreshComplete`.
- `freeink-sdk/.../src/FreeInkDisplay.cpp`: scopes + `DriverStart` in every facade display entry.
- `src/util/LatencyTrace.{h,cpp}` (new): static event buffer, `freeink_latency_mark`, `dump()`.
- `src/main.cpp` (`InputEdge`), `src/activities/ActivityManager.cpp` (`RenderStart`, `RenderDone`, dump), `src/activities/reader/EpubReaderActivity.cpp` (`PageTurn`).
- `platformio.ini`: `[env:x4-pro-latency]`.

Timestamped event lines (added 2026-10-06, **built, not yet flashed**):
`LATENCY_LOG(...)` in `src/util/LatencyTrace.h` prints `LAT: @<ms> ...` immediately via
`BoardConfig::serialTransport()`. It cannot use `Serial`: in any file that includes
`Logging.h`, `Serial` is the `MySerialImpl` proxy, whose body only exists under
`SIMULATOR`, so the link fails on hardware (`undefined reference to MySerialImpl::instance`).
Sites: `src/main.cpp` (every input edge with btn/touch/home-key flags; Home key tap
pending / single fires / double / long), `src/MappedInputManager.cpp` (touch long-press
fired → contact suppressed), `src/activities/ActivityManager.cpp` (`render <activity>`).
Next step is `pio run -e x4-pro-latency -t upload` and a repeat of the Home key and
Library tile taps.

Capture: `scripts/capture_latency_trace.py [/dev/cu.usbmodemXXXX] [seconds]` filters
`LAT:` / `Wait complete` lines to stdout and appends everything to `serial_full.log`.
It reconnects when the port drops and reopens after 30 s of silence. Gotchas: a device
in dashboard sleep does not answer esptool ("No serial data received"), wake it first;
dashboard sleep drops the USB port; after a reset a stale open handle reads nothing.

Remove by deleting the new files (`src/util/LatencyTrace.*`,
`scripts/capture_latency_trace.py`), the `[env:x4-pro-latency]` block, and the
`FREEINK_LAT_*` / `LATENCY_MARK` / `LATENCY_LOG` lines; nothing else was touched.

## 7. Unverified

- ~~Which controller this unit probes to~~ Measured: UC8279.
- ~~Every waveform duration, render duration~~ Measured for B/W renders (§3); AA page turns and SD read time still unmeasured.
- ~~Whether GPIO12/GPIO11 are FSPI IOMUX pins on the S3~~ Verified: they are (`SPI2_IOMUX_PIN_NUM_CLK/MOSI`), but the Arduino SPI HAL routes them through the GPIO matrix anyway; see #1. The matrix-routed ceiling for a write-only link is still unverified.
- Time taken by the 80 → 240 MHz clock switch.
- GT911 report interval and I2C transaction time; Wire-mutex contention magnitude.
- ~~Runtime free internal heap on the X4 Pro~~ Measured: 195 KB free, 147 KB largest block.
- Whether the user's reader font takes the TrueType path or a monochrome `.cpfont`
  (decides whether the AA pipeline runs at all).
- Wi-Fi task priority (closed library).
