# X4 Pro display pipeline: framebuffer ready -> BUSY released

Read-only investigation. Repo root `/Users/bzatrok/dev/CrossInk`. SDK submodule pinned at `918136bf`.
Abbreviations: `FID/` = `freeink-sdk/libs/display/FreeInkDisplay/`, `BC.h` = `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`,
`XTD.cpp` = `freeink-sdk/libs/hardware/XteinkDetect/src/XteinkDetect.cpp`.
Arduino core references are to the installed package `~/.platformio/packages/framework-arduinoespressif32` (package.json version 3.3.7).

Headline: the X4 Pro profile defaults to SSD1677, but a boot probe can swap in a UC8179 or UC8279 driver. All three drivers are linked into the `x4-pro` build. Which controller this specific unit carries is UNVERIFIED from source (a saved project note dated 2026-10-05 says UC8279; not confirmed from logs this session).

---

## 1. Controller IC and driver selection

- X4 Pro profile: `DisplayController::SSD1677`, 800x480 — `BC.h:1629-1635`.
- Display pins `{sclk 12, mosi 11, cs 13, dc 18, rst 14, busy 6, powerEnable PIN_UNASSIGNED}` — `BC.h:1643`. `DisplayPins` struct field order — `BC.h:467-475`.
- `FREEINK_DEVICE_X4PRO` enables `FREEINK_DRIVER_SSD1677` (`BC.h:120-123`) and `FREEINK_DRIVER_UC8179` + `FREEINK_DRIVER_UC8279_X4` (`BC.h:152-155`).
- Controller enum incl. UC8179/UC8279 — `BC.h:428-447`. `displayControllerVariant` field — `BC.h:774-777`; X4 Pro sets 0 ("filled by the boot probe") — `BC.h:1739`.
- Runtime probe: `src/main.cpp:1184-1190` calls `freeink::applyXteinkDisplayController()` before `display.begin()` (S3 path). C3 path equivalent at `lib/hal/HalGPIO.cpp:172-174`.
  - `applyXteinkDisplayController` — `XTD.cpp:425`. For SSD1677 default: probe (`probeSaysUltraChip`, `XTD.cpp:392`, called `XTD.cpp:499-501`); if UltraChip, VER byte2 in {02,03,41,42,67,68,69} -> UC8279 else UC8179 (`XTD.cpp:503-528`; ID sets `XTD.cpp:384-387`).
- Driver pick in facade: `FreeInkDisplay::selectDriver()` — X4 panel branch: UC8179 if `ACTIVE.displayController == UC8179`, UC8279X4 if `== UC8279`, else SSD1677 — `FID/src/FreeInkDisplay.cpp:146-170`. Called from `begin()` — `FreeInkDisplay.cpp:193-195`.
- Driver singletons: `ssd1677Driver()` — `FID/src/driver/Ssd1677Driver.cpp:815-818`; `uc8179Driver()` — `FID/src/driver/Uc8179Driver.cpp:806`; `uc8279X4Driver()` — `FID/src/driver/Uc8279X4Driver.cpp:843-846`.
- SSD1677 config selection: X4 Pro -> `ssd1677DefaultConfig()` unless `FREEINK_X4PRO_FAST_DU_SHORTCUT` — `Ssd1677Driver.cpp:797-804`. That flag is not in `platformio.ini`.

### SSD1677 sequences (`FID/src/driver/Ssd1677Driver.cpp`)
- Command constants — `:13-37`.
- Init `initController` — `:197-241`: `0x12` SWRESET, `delay(10)`, waitBusy; `0x18`=0x80; `0x0C`=AE C7 C3 C0 80; `0x01`=(h-1) lo, hi, 0x02 (|0x01 if mirrorY); `0x3C`=0x80; `setRamArea` full; `0x46`=F7 + waitBusy; `0x47`=F7 + waitBusy. Arms `_needsInitialFull` because fullSeqOverride != 0 (`:240`).
- `begin` = `bus.reset()` + `initController` — `:192-195`.
- RAM window `setRamArea` — `:243-279`: `0x11`=0x01 (X inc, Y dec), `0x44` X range, `0x45` Y range (y flipped `y = _h - y - h`, `:255`), `0x4E`, `0x4F`.
- Write RAM `writeRam` — `:281-284`: `cmd(0x24 BW | 0x26 RED)` then `data(ptr, size)` (size cast to uint16_t; 48000 fits).
- Refresh `refresh` — `:286-397`: `0x21` CTRL1 (0x00 Fast / 0x40 bypass-RED otherwise, `:292-293`), seqOverride path (`:316-356`): border `0x3C`, Half adds `0x1A`=halfRefreshTemp, `0x22`=seq, `0x20`, waitRefreshComplete unless async. Fallback incremental path `:358-391` (not used by X4 Pro default config except custom-LUT).
- Default config values — `:53-70`: booster AE C7 C3 C0 80, scan 0x02, borderInit 0x80, halfTemp 0x5A, grayLut `lut_grayscale`, full 0xF7, fast 0xFC, half 0xD7, borders C0/C0/C0, gray border C0, grayPowerUpFirst false, absoluteGrayscale true.
- Deep sleep — `:762-772`: `powerOffController` (`0x3C`=0x80, `0x22`=0x03, `0x20`, `delay(200)`, waitBusy, `:408-420`) then `0x10`=0x03.

### UC8179 sequences (`FID/src/driver/Uc8179Driver.cpp`)
- Command constants — `:18-37`. Config — `:86-104`: psr0 0x3F, psr1 0x0A, pfs 0x20, btst 25 25 3C 25, gateScan 0x02, ccset 0x02, tsset 0x1E, tssetFast 0x5A, cdiActive 0x29, cdiIdle 0xA9, tresHeight 600, powerSave 0x22.
- Init — `:125-171`: PSR `0x00`=3F 0A; TRES `0x61`=03 20 02 58 (800x600); GSST `0x65`=00 00 00 00; PFS `0x03`=20; BTST `0x06`=25 25 3C 25; `0xE1`=02; `0xE3`=22.
- `begin` — `:219-234`: PSRAM `_grayBase` alloc, `bus.reset(50)`, init.
- Refresh setup `startBwRefresh` — `:414-450`: CDI `0x50`=29 07; `0xE0`=02; `0xE5`=5A fast / 1E full; PSR=1F 0A (REG cleared -> OTP); fast adds PFS `0x03`=20, `0xE1`=02; PON `0x04` + waitBusy if off; fast: PTIN `0x91` (no PTL window); DRF `0x12`; spin up to 50 ms for BUSY to drop.
- Planes: DTM1 `0x10` = OLD, DTM2 `0x13` = NEW — `:24-25`.
- Finish `displayFinish` — `:452-476`: waitRefreshComplete; PTOUT `0x92` if partial; CDI=A9 07; stream DTM1 from fb; POF `0x02` + waitBusy if turnOff.
- Deep sleep — `:488-502`: POF if on, `0x07`=0xA5.

### UC8279X4 sequences (`FID/src/driver/Uc8279X4Driver.cpp`)
- Command constants — `:26-43`. Config — `:213-233`: psr0 0x37, psr1 0x4D, pfs 0x20, pll 0x0E, gateScan 0x02, ccset 0x02, tsset 0x1E, tssetFast 0x5A, cdiAa 0x97, cdiBwFull 0x97, cdiBwFast 0xD7, tresHeight 600, gateOffset 120.
- Init — `:253-284`: PSR=37 4D; TRES 800x600; GSST 0x0x4; PFS 20; PLL `0x30`=0E (skipped on X4 Classic, `:274-277`); `0xE1`=02.
- `begin` — `:286-305`: `bus.reset(50)`, init, PSRAM `_grayBase` (malloc fallback).
- Refresh setup `startBwRefresh` — `:459-510`: CDI (1 byte) D7 fast / 97 full; `0xE0`=02; `0xE5`=5A/1E; fast adds PFS, `0xE1`; PON if off (`powerOnIfNeeded`, `:387-392`); fast: PTIN `0x91` + PTL `0x90` window x 0..799, y 120..599, 0x01; PSR=17 4D; DRF `0x12`; spin up to 50 ms for BUSY to drop.
- Finish — `:512-530`: waitRefreshComplete; PTOUT if partial; stream DTM1 from fb; POF if turnOff.
- Deep sleep — `:542-554`: POF if on, `0x07`=0xA5.

---

## 2. SPI bus

- Clock: `XTEINK_DISPLAY_SPI_HZ = 10000000u` — `BC.h:882`, used by X4 Pro at `BC.h:1644`. Drivers use the board value when non-zero: SSD1677 default 40 MHz otherwise (`Ssd1677Driver.cpp:186-188`); UC8179 16 MHz (`Uc8179Driver.cpp:115-118`); UC8279X4 16 MHz (`Uc8279X4Driver.cpp:244-247`). Effective: 10 MHz for all three.
- Mode/bit order: `SPISettings(spiHz, MSBFIRST, SPI_MODE0)` — `FID/src/bus/EpdBus.cpp:85`.
- Host: global Arduino `SPI` object; `SPIClass SPI(FSPI)` on non-ESP32 targets — Arduino `libraries/SPI/src/SPI.cpp:353-358`. Bus started with `SPI.begin(sclk, spiMiso(-1), mosi, cs)` — `EpdBus.cpp:105`. MISO unused (`PanelDriver.h:40`).
- Transfer API: Arduino `SPI.transfer(byte)` for single bytes and `SPI.writeBytes(ptr, len)` for bulk — `EpdBus.cpp:157-229`. No `spi_device_transmit`/`spi_device_queue_trans`/`writePixels`.
- DMA: none. `SPIClass::writeBytes` -> `spiWriteNL` (`SPI.cpp:272-279`), which loads up to 64 bytes (16 words) into the FIFO, sets `cmd.usr`, and busy-spins until done, per chunk — `cores/esp32/esp32-hal-spi.c:1456-1493`. CPU is busy for the whole transfer.
- CS/DC: manual GPIO (`pinMode` at `EpdBus.cpp:107-108`, `digitalWrite` toggles throughout).
  - `cmd(c)`: own `beginTransaction`, DC low, CS low, 1 byte, CS high — `EpdBus.cpp:157-164`.
  - `data(uint8_t)`: own transaction + CS toggle per byte — `EpdBus.cpp:166-173`.
  - `data(ptr,len)`: one transaction, CS low for the whole buffer — `EpdBus.cpp:175-182`.
  - `cmdData`: command + payload in one CS-low — `EpdBus.cpp:184-195`.
  - `beginTxn/rawWriteBytes/endTxn`: CS held low across many writes — `EpdBus.cpp:202-229`.
- Chunking per driver:
  - SSD1677: one `writeBytes(fb, 48000)` per plane under one CS-low (`Ssd1677Driver.cpp:281-284`) = 750 FIFO loads. LUT upload is per-byte transactions: 105 bytes to `0x32` each in its own transaction (`Ssd1677Driver.cpp:734-736`), plus `0x03`, `0x04` x3, `0x2C` (`:739-748`). Grayscale absolute writes use a 128-byte inverted chunk (`:632-648`).
  - UC8179 `streamPlane`: `cmd`, then one CS-low; 480 x `rawWriteBytes(src, 100)` direct from framebuffer (rows reversed), then 120 rows of 0xFF padding from a 128-byte stack row; 60,000 bytes per plane — `Uc8179Driver.cpp:303-328`. Inverted variant copies through a 128-byte chunk (`:310-318`).
  - UC8279X4 `streamPlane`: `cmd`, one CS-low; 120 pad rows 0xFF, 480 visible rows each copied byte-by-byte into a 128-byte stack row then `rawWriteBytes(row, 100)`, then pad to 600 (0 extra rows); 60,000 bytes per plane — `Uc8279X4Driver.cpp:321-355`.
  - `fillPlane`: 128-byte stack chunk, `rawWriteBytes` per row under one CS-low — `EpdBus.cpp:438-454`.
- App-side bus lock: `HalSpiBus::Lock` (recursive FreeRTOS mutex, `portMAX_DELAY`) — `lib/hal/HalSpiBus.cpp:5-34`. Taken in `HalDisplay::begin` (`lib/hal/HalDisplay.cpp:27`), `displayBuffer` (`:76`), `setInverted` (`:86`), `restoreVisibleFrame` (`:94`), `displayGrayscaleBase(mode,...)` (`:118`), `refreshDisplay` (`:124`), `deepSleep` (`:136`), `displayGrayBuffer` (`:177`), `writeGrayscalePlaneStrip` (`:182`). NOT taken in `displayBufferAsync` (`:99-105`), `waitRefreshComplete` (`:107`), `copyGrayscale*` (`:146-148`, `:170-172`), `cleanupGrayscaleBuffers` (`:174`), `displayGrayscaleBase(fallback,...)` (`:150-162`).

---

## 3. Framebuffer

- Size: driver geometry -> width 800, widthBytes 100, height 480, bufferSize 48,000 — `FreeInkDisplay.cpp:210-214`; SSD1677 geometry `Ssd1677Driver.cpp:173-178,190`; UC drivers `Uc8179Driver.cpp:107-113,120`, `Uc8279X4Driver.cpp:236-242,249`. Compile-time cap `MAX_FRAMEBUFFER_BYTES` — `BC.h:1891-1896`, exposed as `MAX_BUFFER_SIZE` — `FID/include/FreeInkDisplay.h:90`.
- Allocation: `allocFrameBufferStorage()` — `heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM)` under `FREEINK_FB_PSRAM`, fallback `malloc` — `FreeInkDisplay.cpp:378-390`. Called once in `begin()`, then `memset(0xFF)` — `FreeInkDisplay.cpp:221-227`. Pointers `frameBuffer0`/`frameBuffer` — `FreeInkDisplay.h:494-495`.
- `-DFREEINK_FB_PSRAM=1` in x4-pro env (platformio.ini `[env:x4-pro]` build_flags). `-DBOARD_HAS_PSRAM` comes from the board JSON `esp32-s3-devkitc1-n16r8.json:12`.
- Buffer count: one. `-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1` in `[base]` — `platformio.ini:35`. `frameBuffer1`/`frameBufferActive` compiled out — `FreeInkDisplay.h:496-499`. Drivers receive `prev = nullptr` — `FreeInkDisplay.cpp:602`.
- Extra framebuffer-sized buffers:
  - Async shadow: lazy `malloc(bufferSize)` only on the shadowed async path — `FreeInkDisplay.cpp:666-669`. The HAL uses the no-shadow variant (`HalDisplay.cpp:104`), so this is not allocated by CrossInk's reader path. Whether a 48 KB plain `malloc` lands in PSRAM: UNVERIFIED (x4-pro env has no `custom_sdkconfig`; `CONFIG_SPIRAM_USE_MALLOC`/threshold for the prebuilt libs not checked).
  - UC driver `_grayBase` (48,000 bytes, PSRAM): `Uc8179Driver.cpp:227-230`, `Uc8279X4Driver.cpp:299-302`. `memcpy(_grayBase, fb, 48000)` on every `displayStart` — `Uc8179Driver.cpp:361-364`, `Uc8279X4Driver.cpp:406-409`.
- Old-image plane handling:
  - SSD1677 Fast (single buffer): writes BW `0x24` only; RED `0x26` written only if `prev != nullptr` (never on X4 Pro) — `Ssd1677Driver.cpp:526-533`. Half/Full write both planes before refresh — `:523-525`. After every blocking refresh with `prev == nullptr`, BOTH planes are rewritten from fb — `:543-547`. Async path skips that resync (`!async`), and `displayFinish` rewrites only if `_pendingFrameSync` (blackPulseClean, false for X4 Pro) — `:436-450`.
  - UC drivers: DTM2 (new) always streamed; DTM1 (old) left as-is on Fast, `~fb` on Half, 0xFF fill on Full — `Uc8179Driver.cpp:391-402`, `Uc8279X4Driver.cpp:433-447` (UC8279X4 also writes `~fb` on Fast right after AA, `:443-447`). After refresh `displayFinish` always rewrites DTM1 from fb — `Uc8179Driver.cpp:466`, `Uc8279X4Driver.cpp:521`.
- Bytes on the wire per blocking refresh (arithmetic from the paths above; command bytes excluded; not measured):

| Driver | Mode | Bytes | Wire time at 10 MHz |
| --- | --- | --- | --- |
| SSD1677 | Fast | 48,000 + 96,000 = 144,000 | ~115 ms |
| SSD1677 | Half/Full | 96,000 + 96,000 = 192,000 | ~154 ms |
| UC8179/UC8279X4 | Fast | 60,000 + 60,000 = 120,000 | ~96 ms |
| UC8179/UC8279X4 | Half/Full | 60,000 x 3 = 180,000 | ~144 ms |

- Bit layout: row-major, row stride 100 bytes, MSB = leftmost pixel, bit set = white, clear = black — `lib/GfxRenderer/GfxRenderer.cpp:957-963`; white init `FreeInkDisplay.cpp:227`. Orientation is software rotation before the write — `GfxRenderer.cpp:935`. Renderer grabs the pointer in `GfxRenderer::begin` — `GfxRenderer.cpp:239-250`.
- Panel mapping: board mount `NO_FLIP` — `BC.h:1712`. SSD1677 flips Y via RAM window (`Ssd1677Driver.cpp:254-255`), mirrorX via data-entry mode (`:249-252`). UC8179 sends rows bottom-up, horizontal via PSR SHL (`Uc8179Driver.cpp:300-308`, config comment `:88-89`). UC8279X4 sends rows forward with 120-gate offset; `FREEINK_UC8279X4_ROWREV`/`XMIRROR` default 0 — `Uc8279X4Driver.cpp:15-20,330-353`.

---

## 4. Waveform / LUT selection

### SSD1677 (X4 Pro default config) — B/W all OTP waveforms

| Mode | Sequence | Source |
| --- | --- | --- |
| FULL | `0x21`=40, `0x3C`=C0, `0x22`=F7, `0x20` | `Ssd1677Driver.cpp:59,292-293,316-338` |
| HALF | `0x21`=40, `0x3C`=C0, `0x1A`=5A, `0x22`=D7, `0x20` | `Ssd1677Driver.cpp:61,332-338` |
| FAST | `0x21`=00, `0x3C`=C0, `0x22`=FC, `0x20` | `Ssd1677Driver.cpp:60,316-338` |
| Overlay gray (AA) | upload `lut_grayscale` (`0x32` 105 B, `0x03`, `0x04` x3, `0x2C`, `0x3C`=C0), then `0x22`=CC, `0x20` | `Ssd1677Driver.cpp:671-714,374-381,727-760` |
| Factory 4-level | `lut_factory_quality`, `0x21`=00, `0x22`=CC, `0x20` | `Ssd1677Driver.cpp:685-703` |

- LUT tables in source: `lut_grayscale` — `FID/src/lut/Ssd1677Luts.h:11`; `lut_factory_quality` — `Ssd1677Luts.h:130` (also sticky/metalio variants `:56,94,152,174`, not used on X4 Pro).
- Opt-in `FREEINK_X4PRO_FAST_DU_SHORTCUT` -> Fast uses incremental `0x22`=0x1C (|C0 when powering on) — `Ssd1677Driver.cpp:137-140,166-171,384-386`. Not set.
- Grayscale capabilities: Overlay {OverlayMasks, Separate, strip=true, asyncBase=true} and Absolute {AbsolutePlanes, Combined, strip=true} — `FID/src/driver/Ssd1677Driver.h:104-109`; struct field order `FID/include/GrayscaleCapabilities.h:15-28`.

### UC8179 — B/W OTP (PSR REG bit cleared at refresh, `Uc8179Driver.cpp:424`)
- FAST: TSSET 5A + PFS/E1 + PTIN + DRF; FULL: TSSET 1E, DTM1 white; HALF: TSSET 1E, DTM1 = ~target — `Uc8179Driver.cpp:380-407,414-450`.
- Custom LUTs in source: `kGrayLuts` (AA, 5 x 42 B, regs 0x20-0x24) — `:57-63`; `kDarkGrayLut` — `:70-71`; `kGrayPreBwMid` (post-AA transition) — `:77-83`; `kUltraChipDirectGray` + `kUc8179DirectGrayConfig` — `FID/src/lut/UltraChipDirectGrayLuts.h:11,20`.
- AA path `displayGray` — `Uc8179Driver.cpp:660-745`: PSR 3F (REG=1), 5 LUTs, CDI 29 07, optional PLL 40 Hz for absolute images (`:719-723`, restored `:732-735`), PON, DRF, no POF.
- Post-AA transition `transitionGrayscaleBase`/`runGrayscalePrecondition` — `:249-279`, `:509-558`.
- Capabilities: all strip/asyncBase/staging false — `FID/src/driver/Uc8179Driver.h:87-97`.

### UC8279X4 — B/W OTP (PSR 17 at refresh, `Uc8279X4Driver.cpp:499-501`)
- FAST: CDI D7, TSSET 5A, PFS/E1, PTIN + PTL window, DRF; FULL: CDI 97, TSSET 1E, DTM1 white; HALF: same as Full but DTM1 = ~target — `:422-457,459-510`.
- Custom LUTs in source: `kXtfAa02`/`kXtfAa68` (AA, 5 x 49 B) — `:50-63`, selected by `displayControllerVariant` in `selectAaLuts` — `:79-89`; `kXtfPreBwMid` — `:70-77`; `kQualityBank` built at compile time from `kUc8279X3_Xth4` (`FID/src/lut/Uc8279X3Luts.h:99`) at `FREEINK_UC8279X4_GRAY_SPEED` 60 — `:100-209`.
- Quality bank auto-selected when MSB mask coverage > 25% (`FREEINK_UC8279X4_QUALITY_COVERAGE_PCT`) — `:619-637,661-672`.
- AA path `displayGray` — `:650-721`: PSR 37, LUTs, CDI 97, PON, PSR rewrite, DRF, waitBusy, no POF unless turnOff, restore base to DTM1+DTM2.
- Variant 0x67 reports no grayscale — `:568`.
- `FID/src/lut/Uc8279X4VendorLuts.h` (`kZhxUc8279Xth4Regs` `:19`, `kQyUc8279Xth4` `:48`) is not included by any file.

### App-facing API
- Modes `FULL_REFRESH, HALF_REFRESH, FAST_REFRESH` — `FID/include/FreeInkDisplay.h:34`; HAL mirror `lib/hal/HalDisplay.h:17-21`; mapping `FreeInkDisplay.cpp:62-71`, `HalDisplay.cpp:63-73`.
- Entry points: `displayBuffer` (`FreeInkDisplay.h:160`), `displayBufferAsync` (`:170`), `displayBufferAsyncNoShadow` (`:178`), `refreshBusy` (`:180`), `waitRefreshComplete` (`:184-187`), `triggerDisplay`/`completeDisplay` (`:214-215`), `triggerDisplayAsync`/`finishDisplayAsync` (`:231-232`), `displayGrayscaleBase` (`:128,132`), `displayGrayBuffer` (`:274`), `setCustomLUT` (`:297`), `requestResync`/`skipInitialResync` (`:280-281`), `restoreVisibleFrame` (`:252`).

---

## 5. Full vs partial decision logic

App:
- Reader cadence `ReaderUtils::displayWithRefreshCycle` — `src/activities/reader/ReaderUtils.h:238-255`: countdown < 0 -> `manualScreenRefreshMode()`; <= 1 -> HALF; else FAST. Resets to `SETTINGS.getRefreshFrequency()` after HALF, else decrements.
- `getRefreshFrequency()` — `src/CrossPointSettings.cpp:1247-1265`: 1/5/10/15/30; NEVER -> `INT_MAX` when `Frontlight.present()` (X4 Pro has a frontlight, `BC.h:1709`). Default `REFRESH_15` — `src/CrossPointSettings.h:582`.
- `manualScreenRefreshMode()` — `src/GlobalActions.h:13-20`: HALF only under `FREEINK_DEVICE_X4` + `deviceIsX4()`; X4 Pro returns FULL.
- EPUB page branch — `src/activities/reader/EpubReaderActivity.cpp:7409-7469`: image pages force `pagesUntilFullRefresh = 1` (`:7449`); grayscale pages at countdown <= 1 do HALF + `preconditionGrayscale` (`:7451-7457`); overlap/async only when strip grayscale + asyncBase (`:7410-7412,7458-7459`); else `displayGrayscaleBase(FAST)` (`:7464`); plain pages via `displayWithRefreshCycle` (`:7468`).
- TXT/XTC readers: `TxtReaderActivity.cpp:889`, `XtcReaderActivity.cpp:1296-1302,1389`.

SDK facade:
- FAST -> HALF when inversion just changed (`_inversionDirty`) — `FreeInkDisplay.cpp:597-599`.

SSD1677 driver:
- `requestResync()` sets `_needsGrayClear` -> next Fast promoted to Half — `Ssd1677Driver.h:98`, `Ssd1677Driver.cpp:456-459`.
- `_needsInitialFull` one-shot (first paint after `begin()`): Fast -> Half — `Ssd1677Driver.cpp:240,467-483`.
- The `!_isScreenOn && fullSeqOverride == 0` cold-start branch (`:484-491`) does not fire on X4 Pro (fullSeqOverride = 0xF7).
- Leaving grayscale: Fast -> Half — `:499-504`.
- `restoreVisibleFrame` clears `_needsInitialFull` — `:616-624`.

UC drivers:
- Partial (`fast`) only if Fast && !`_needFullClear` && `_oldPlaneValid` — `Uc8179Driver.cpp:381`, `Uc8279X4Driver.cpp:423`.
- `_needFullClear` true at begin (`Uc8179Driver.cpp:224`, `Uc8279X4Driver.cpp:292`) and after `requestResync` (`Uc8179Driver.cpp:478-484`, `Uc8279X4Driver.cpp:532-538`); cleared by `skipInitialResync` (`:486` / `:540`) and after each finish.
- Post-AA Fast -> `transitionGrayscaleBase` (non-flashing pre-BW-mid LUT) — `Uc8179Driver.cpp:241-244`, `Uc8279X4Driver.cpp:313-316`.
- No `restoreVisibleFrame` override in either UC driver -> base returns false — `FID/src/driver/PanelDriver.h:114-118`.

HAL:
- `HalDisplay::begin` calls `requestResync()` on wake reasons PowerButton / AfterFlash / Other; `seamless=true` calls `skipInitialResync()` — `lib/hal/HalDisplay.cpp:26-49`.

No partial-refresh counter in the SDK for these three drivers. `setHoldPeriodicFullRefresh` only affects UC8279C — `FreeInkDisplay.h:70-74`, `FreeInkDisplay.cpp:1046-1048`.

---

## 6. BUSY pin

- Polarity: SSD1677 `ActiveHigh` — `Ssd1677Driver.h:81`; UC8179/UC8279X4 `UcIdleHigh` — `Uc8179Driver.h:64`, `Uc8279X4Driver.h:69`. Enum — `EpdBus.h:16-21`. Pin mode `INPUT` (not pull-up for these) — `EpdBus.cpp:118`.
- No busy hooks installed by CrossInk (`rg setBusyWait src lib include` returns nothing). Hook API — `FreeInkDisplay.h:306,311-313`, `EpdBus.h:84-95`.
- SSD1677 refresh wait = `EpdBus::waitRefreshComplete` ISR path — `EpdBus.cpp:359-403`: drain semaphore, `attachInterrupt(busy, CHANGE)`; if not yet HIGH, `xSemaphoreTake` 20 ms for the assert edge (returns early if none); then loop `xSemaphoreTake(..., 30000 ms)` until LOW; detach. Task sleeps (no polling). ISR — `EpdBus.cpp:71-78`.
- SSD1677 command waits (`waitBusy` ActiveHigh) — `EpdBus.cpp:242-253`: poll, `delay(1)`, timeout 30,000 ms.
- UC refresh wait: `waitRefreshComplete` falls through to `waitBusy` for `UcIdleHigh` — `EpdBus.cpp:328-331`. `waitBusy` UcIdleHigh — `EpdBus.cpp:278-294`: `delay(1)`, then `while (BUSY == LOW) delay(1)`, NO timeout.
- UC `startBwRefresh` spin after DRF: `while (BUSY HIGH && <50 ms) delay(1)` — `Uc8179Driver.cpp:445-449`, `Uc8279X4Driver.cpp:505-509`.
- Blocking: `display()` = start + finish in one call for all three — `Ssd1677Driver.cpp:422-424` (blocking `refresh`), `Uc8179Driver.cpp:245-246`, `Uc8279X4Driver.cpp:317-318`. Caller is the render task, priority 1, pinned to core 1 — `src/activities/ActivityManager.cpp:452-469,476-491`. Stack 24576 on S3 reader / 8192 network — `src/main.cpp:372-376,1206`.
- Async path: `displayBufferAsyncNoShadow` -> `displayAsyncImpl(noShadow)` returns after `displayStart` with `_refreshPending` — `FreeInkDisplay.cpp:647-657,802-804`. `refreshBusy()` — `:583-588`. Finish via `waitRefreshComplete()` -> `finishDisplayAsync()` + `syncPendingAsync()` -> `driver.displayFinish` — `FreeInkDisplay.h:184-187`, `FreeInkDisplay.cpp:553-567`. Every blocking op drains a pending async first (`syncPendingAsync`, e.g. `:596,646,705,818,834,975,1060`).
- Who uses async: only `EpubReaderActivity.cpp:7459` (via `ReaderUtils.h:245-246`), gated on strip grayscale + asyncBase (`EpubReaderActivity.cpp:7410-7412`). True only for SSD1677 (`Ssd1677Driver.h:108`); UC capabilities are false (`Uc8179Driver.h:95-96`, `Uc8279X4Driver.cpp:573`). On UC units every reader refresh is blocking. Finishes at `src/activities/reader/EpubGrayscale.cpp:71,89`.
- `GfxRenderer::displayBufferAsync` falls back to blocking when fading fix is on — `GfxRenderer.cpp:2432-2440`.

---

## 7. Timing constants and delays

| Delay | Value | Where |
| --- | --- | --- |
| Reset pulse | `delay(10)` x3 (HIGH, LOW, HIGH) | `EpdBus.cpp:146-151` |
| Reset extra settle | +50 ms on UC begin / direct-gray reconfig | `EpdBus.cpp:152-154`; `Uc8179Driver.cpp:180,211,232`; `Uc8279X4Driver.cpp:294` |
| Power-enable settle | 100 ms (not on X4 Pro: pin unassigned, no `freeink_board_epd_power` defined in CrossInk) | `EpdBus.cpp:95-103`; `BC.h:1643` |
| SSD1677 post-SWRESET | `delay(10)` | `Ssd1677Driver.cpp:204` |
| SSD1677 power-off | `delay(200)` then waitBusy | `Ssd1677Driver.cpp:417-418` |
| Busy poll step | `delay(1)` | `EpdBus.cpp:128,244,262,286,297,303` |
| Busy hook threshold | 20 ms | `EpdBus.h:117` |
| ActiveHigh/ActiveLow/X3 timeouts | 30,000 ms (X3 phase 1: 1,000 ms) | `EpdBus.cpp:252,275,298,311` |
| ActiveHigh grace (slice-hook path only) | 20 ms of `delayMicroseconds(200)` | `EpdBus.cpp:350-355` |
| ISR assert wait / completion take | 20 ms / 30,000 ms per take | `EpdBus.cpp:381,396` |
| UC DRF-start spin | up to 50 ms, `delay(1)` | `Uc8179Driver.cpp:448`; `Uc8279X4Driver.cpp:508` |
| UcIdleHigh wait | `delay(1)` first, no timeout | `EpdBus.cpp:284-294` |

Fading fix interaction: `GfxRenderer::displayBuffer` passes `fadingFix || turnOffScreen` — `GfxRenderer.cpp:2395-2397`; set from `SETTINGS.fadingFix` at `src/main.cpp:1724`. With it on:
- UC: POF + waitBusy after each refresh (`Uc8179Driver.cpp:471-475`, `Uc8279X4Driver.cpp:525-529`) and PON + waitBusy before the next (`Uc8179Driver.cpp:435-439`, `Uc8279X4Driver.cpp:387-392,478`).
- SSD1677 (seq FC does not self-power-off): `_pendingPowerOff` -> `powerOffController` with the 200 ms delay — `Ssd1677Driver.cpp:344-350,548-551`.

CPU clock: render runs under `HalPowerManager::Lock` (restores normal freq) — `ActivityManager.cpp:485`, `lib/hal/HalPowerManager.cpp:313-327`; low-power freq 80 MHz on PSRAM boards — `lib/hal/HalPowerManager.h:38-42`.

---

## 8. Existing instrumentation

- `EpdBus::waitBusy` and `waitRefreshComplete` print `"[<millis>]   Wait complete: <tag> (<n> ms)"` when `tag != nullptr && Serial` — `EpdBus.cpp:319-321,401-403`. Independent of `LOG_LEVEL`. Not printed for X3TwoPhase without a LOW edge (`:317`).
  - UC tags (examples): `" 8179_PON"` `Uc8179Driver.cpp:437`, `" 8179_DRF"` `:456`, `" 8179_POF"` `:473`; `" 8279x4_PON"` `Uc8279X4Driver.cpp:478`, `" 8279x4_DRF"` `:516`, `" 8279x4_POF"` `:527`, `" 8279x4_gray"` `:686`.
  - SSD1677 tags: `"refresh"` `Ssd1677Driver.cpp:339,391,437`, `" CMD_SOFT_RESET"` `:205`, `" CMD_AUTO_WRITE_BW_RAM"` `:230`, `" CMD_AUTO_WRITE_RED_RAM"` `:234`, `"factory_gray"` `:700`, `" display power-down"` `:418`, `"gray power-on"` `:404`.
  - Whether HWCDC `Serial` (x4-pro: `-DARDUINO_USB_MODE=1`, `Serial.begin` at `src/main.cpp:1262`) is truthy without a host attached: UNVERIFIED.
- `SSD1677_PROBE_DEBUG` (off; not in platformio.ini): per-refresh ms via `esp_rom_printf` — `Ssd1677Driver.cpp:288-291,351-354,393-396`; facade traces — `FreeInkDisplay.cpp:593-595,808-810,828-830`.
- Probe diagnostics at boot (`Serial.printf` with `millis()`) — `XTD.cpp:434-438,513-519`.
- App: x4-pro sets `-DLOG_LEVEL=1` so `LOG_DBG` is compiled out — `lib/Logging/Logging.h:27-44`. No `millis()`/`esp_timer_get_time` timing around display calls in `src/activities/ActivityManager.cpp`, `src/activities/reader/EpubReaderActivity.cpp`, `src/activities/reader/ReaderUtils.h`, or `lib/GfxRenderer/GfxRenderer.cpp` (only unrelated prewarm timing at `EpubReaderActivity.cpp:2783`).

---

## Call chain (app -> SDK -> BUSY)

Blocking page/screen refresh:
1. `ActivityManager::renderTaskLoop` -> `currentActivity->render()` (`src/activities/ActivityManager.cpp:476-491`; also calls `display.setInverted(...)` each frame `:488`).
2. Activity -> `ReaderUtils::displayWithRefreshCycle` (`ReaderUtils.h:238`) or `renderer.displayBuffer(mode)`.
3. `GfxRenderer::displayBuffer` (`lib/GfxRenderer/GfxRenderer.cpp:2395-2397`).
4. `HalDisplay::displayBuffer` + `HalSpiBus::Lock` (`lib/hal/HalDisplay.cpp:75-83`).
5. `EInkDisplay` alias (`FID/include/EInkDisplay.h:14`) -> `FreeInkDisplay::displayBuffer` (`FreeInkDisplay.cpp:590-625`) -> `_driver->display(_bus, frameBuffer, nullptr, mode, turnOff)` (`:602`).
6. Driver:
   - SSD1677: `display` -> `displayImpl` -> `setRamArea` -> `writeRam(0x24[,0x26])` -> `refresh` -> `0x22`/`0x20` -> `EpdBus::waitRefreshComplete` (ISR) -> post-resync `writeRam(0x24)`, `writeRam(0x26)` (`Ssd1677Driver.cpp:422-552`).
   - UC8179: `display` -> `displayStart` (`memcpy _grayBase`, `streamPlane(DTM2)`, optional DTM1, `startBwRefresh`: CDI/E0/E5/PSR/[PFS,E1]/[PON]/[PTIN]/DRF) -> `displayFinish` (`waitRefreshComplete` -> `waitBusy` poll, PTOUT, CDI idle, `streamPlane(DTM1)`, [POF]) (`Uc8179Driver.cpp:236-476`).
   - UC8279X4: same shape (`Uc8279X4Driver.cpp:307-530`).

Async (SSD1677 only in practice):
- `displayWithRefreshCycle(..., true)` -> `GfxRenderer::displayBufferAsync` (`GfxRenderer.cpp:2432-2440`) -> `HalDisplay::displayBufferAsync` (`HalDisplay.cpp:99-105`, no lock) -> `FreeInkDisplay::displayBufferAsyncNoShadow` (`FreeInkDisplay.cpp:802-804`) -> `displayAsyncImpl` (`:629-657`) -> `driver.displayStart`. Finish: `GfxRenderer::waitRefreshComplete` (`GfxRenderer.cpp:2442`) -> `HalDisplay::waitRefreshComplete` (`HalDisplay.cpp:107`) -> `syncPendingAsync` -> `driver.displayFinish`.

Bring-up:
- `setupDisplayAndFonts` (`src/main.cpp:1179-1206`) -> `applyXteinkDisplayController` (`:1187`) -> `HalDisplay::begin` (`HalDisplay.cpp:26-49`) -> `FreeInkDisplay::begin` (`FreeInkDisplay.cpp:193-234`: `selectDriver`, `EpdBus::begin`, framebuffer alloc, `driver.begin`) -> `renderer.begin()` (`GfxRenderer.cpp:239-250`) -> `activityManager.begin` (`main.cpp:1206`).

Sleep:
- `display.deepSleep()` (`src/main.cpp:1109`) -> `HalDisplay::deepSleep` (`HalDisplay.cpp:135-138`) -> `FreeInkDisplay::deepSleep` (`FreeInkDisplay.cpp:1058-1062`) -> driver `deepSleep`.
