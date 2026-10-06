# x4-pro FreeRTOS task and core inventory

Read-only. No repository files were changed.

## Key facts

- The render task "ActivityManagerRender" is pinned to core 1 at priority 1. Its stack is 24576 bytes on S3.
- loopTask also runs on core 1 at priority 1. Its stack is 16384 bytes, from the FreeInkUI weak override. The ELF disassembly confirms the value.
- configUSE_TIME_SLICING is 1 and the tick is 1000 Hz. When both tasks are ready, they share core 1 in 1 ms slices.
- loopTask never blocks on RenderLock in its steady-state loop. It uses Try mode there.
- The SDK input task "fi_input" (InputManager::beginAsync) is never started. Nothing calls beginAsync, and the symbol is absent from the x4-pro ELF.
- AudioManager, HapticManager and BleKeyboardHost are not compiled for x4-pro.
- x4-pro does not consume the project `sdkconfig.defaults`. That file is gitignored and stale, left over from a sticky/firmware_tuned build. x4-pro links pioarduino's prebuilt dio_opi libs.

## App and SDK tasks on x4-pro

### ActivityManagerRender

| Field | Value |
| --- | --- |
| Created | `src/activities/ActivityManager.cpp:461`, xTaskCreatePinnedToCore |
| Function | renderTaskTrampoline / renderTaskLoop (`:471`, `:476`) |
| Stack | Bytes, because the IDF stack depth is in bytes. 24576 on S3 (`src/main.cpp:376`). 8192 (`NETWORK_RENDER_TASK_STACK_BYTES`, `main.cpp:372`) only on a non-S3 network resume (`main.cpp:1294`). Passed at `main.cpp:1206`. |
| Priority | 1 |
| Core | 1. `#if defined(configNUM_CORES) && configNUM_CORES > 1` selects 1 (`ActivityManager.cpp:456-460`). configNUM_CORES = configNUMBER_OF_CORES = CONFIG_FREERTOS_NUMBER_OF_CORES = 2 (libs `FreeRTOSConfig.h:98,100`; dio_opi `sdkconfig.h:1233`). |
| Gate | None |
| Lifetime | Once, at boot, in setupDisplayAndFonts |

**Blocks on:**
- ulTaskNotifyTake(portMAX_DELAY) (`:478`)
- RenderLock with portMAX_DELAY (`:481`)
- HalPowerManager::Lock (`:486`)

**Touches display/SPI/framebuffer:** YES. This is the primary panel SPI writer.
- During BUSY the driver polls with delay(1). See EpdBus::waitBusy (`freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp:240-300`) and busyIdle (`EpdBus.h:122-127`). The task yields while it waits.
- X4 Pro display SPI is SCLK12 / MOSI11 / CS13 at 10 MHz (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1645-1646`, `:882`).
- The bus uses Arduino `SPI.beginTransaction` / `writeBytes` (`EpdBus.cpp:158-228`).

### DictLookup

| Field | Value |
| --- | --- |
| Created | `src/util/DictionaryLookupWorker.cpp:14`, xTaskCreateStatic, created lazily |
| Function | taskEntry, then run (`:35`, `:37`) |
| Stack | 4096 bytes, static (`DictionaryLookupWorker.h:26-35`) |
| Priority | 1 |
| Core | Unpinned. IDF xTaskCreateStatic passes tskNO_AFFINITY (`~/.platformio/packages/framework-espidf/components/freertos/FreeRTOS-Kernel/include/freertos/task.h:528-531`). |
| Gate | None |
| Lifetime | Created on the first dictionary lookup. Never deleted. |

**Blocks on:** ulTaskNotifyTake(portMAX_DELAY) (`:39`). The caller waits in waitForOwner with vTaskDelay(1) (`:32`).

**Touches display:** NO. It does SD reads over SDMMC and writes atomics. It does not call requestUpdate (`src/util/DictionaryLookupController.cpp:576-610`).

### dash_backstop

This is an esp_timer, not a task.

| Field | Value |
| --- | --- |
| Created | `src/dashboard/DashboardWake.cpp:57` |
| Timing | One-shot, 60 s (BACKSTOP_SECONDS, `DashboardWake.h:21`) |
| Dispatch | ESP_TIMER_TASK. The callback runs on the esp_timer task. |
| Gate | CROSSINK_APP_CAP_DASHBOARD (`DashboardWake.cpp:3`) |
| Lifetime | Per dashboard wake (`:138`). Deleted in the destructor. |

The callback forces deep sleep (`:39-44`). Per its comment it touches no display and no SD.

No other `xTaskCreate*`, `xTimerCreate` or `esp_timer_create` calls exist in `src/`, `lib/` or `include/`.

## SDK call sites not in the x4-pro build

Verified against the `.pio/build/x4-pro` lib dirs and by running nm on `firmware.elf`.

| Task | Source | Parameters | Why absent |
| --- | --- | --- | --- |
| fi_input | `freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:236` | xTaskCreate, 4096, default priority 2 (`InputManager.h:229`), unpinned, vTaskDelay(15 ms) poll | Only beginAsync creates it, and beginAsync has no caller |
| audio_play | `freeink-sdk/libs/hardware/AudioManager/src/AudioManager.cpp:360` | 8192, priority 10, core 0 | Library not linked |
| ble-conn | `freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp:536` | 4096, priority 3, unpinned | Library not linked |
| HapticManager esp_timer | `freeink-sdk/libs/hardware/HapticManager/src/HapticManager.cpp:133` | n/a | Library not linked |

## Framework and IDF tasks linked into x4-pro

- "sdkconfig.h" in this section means `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/dio_opi/include/sdkconfig.h`.
- IDF source is `~/.platformio/packages/framework-espidf`, version 5.5.2.
- configMAX_PRIORITIES is 25.

### loopTask

- Created at `~/.platformio/packages/framework-arduinoespressif32/cores/esp32/main.cpp:113` with xTaskCreateUniversal.
- Priority 1. Core is ARDUINO_RUNNING_CORE = 1 (`sdkconfig.h:483`).
- Stack is 16384 bytes. The weak override at `freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:13` wins over CONFIG_ARDUINO_LOOP_STACK_SIZE 8192 (`sdkconfig.h:484`). The linked getArduinoLoopTaskStackSize at 0x422d26cc is `movi 1; slli 14`, which equals 16384.
- Polls input via `mappedInputManager.update()` (`src/main.cpp:1698`).

**End-of-loop pacing** (`main.cpp:1957-1981`):
- delay(inputPollDelayMs). The default is 10 ms (`src/activities/Activity.h:59`). KeyboardEntryActivity uses 2 ms on touch hardware.
- After 3 s idle: delay(50) and the CPU drops to 80 MHz (`lib/hal/HalPowerManager.h:39,43`).
- When skipLoopDelay is set: yield() only.

**Also drives the panel directly while holding RenderLock:**
- `main.cpp:587-644` (displayBuffer at `:596` and `:637`)
- The sleep path at `ActivityManager.cpp:1058`, which reaches SleepActivity displayBuffer (`src/activities/boot_sleep/SleepActivity.cpp:719,797,904,925,946`)
- Dashboard fallback (`DashboardWake.cpp:115-117`)
- `main.cpp:1546`, `:1550`

So panel SPI comes from two tasks. Both are on core 1, and RenderLock serializes them.

### Other framework and IDF tasks

**main task (app_main)**
- CPU0 (`sdkconfig.h:1100-1101`), stack 4096 (`:1099`).
- In Arduino it ends after creating loopTask.

**arduino_events**
- `~/.platformio/packages/framework-arduinoespressif32/libraries/Network/src/NetworkEvents.cpp:67`.
- Stack 4096 (`:15`).
- Priority ESP_TASKD_EVENT_PRIO-1 = 19.
- Core ARDUINO_EVENT_RUNNING_CORE = 1 (`sdkconfig.h:486`).
- Created when networking starts.

**sys_evt**
- Priority 20, core 0, stack base 2048.
- Sources: `framework-espidf/components/esp_event/default_event_loop.c:100-103`; `esp_system/include/esp_task.h:48,50-52`; `sdkconfig.h:1098`.

**tcpip_thread**
- Priority 18, CPU0, 4096 (`sdkconfig.h:1256,1318-1320`).

**wifi**
- Pinned to core 0 (`sdkconfig.h:1149`).
- Priority UNVERIFIED. It lives in the closed libnet80211. Creation goes through `framework-espidf/components/esp_wifi/esp32s3/esp_adapter.c:330-332`.

**async_udp**
- `libraries/AsyncUDP/src/AsyncUDP.cpp:214`.
- Priority 3, core 0, 4096 (`sdkconfig.h:491-494`).
- Created on the first AsyncUDP::listen. The symbol is linked. The app caller is UNVERIFIED.

**mdns**
- Priority 1, CPU0, 4096 (`sdkconfig.h:1817-1821`).
- Started by MDNS.begin in:
  - `src/activities/network/CrossPointWebServerActivity.cpp:51`
  - `src/activities/network/CalibreConnectActivity.cpp:87`
  - `src/activities/home_control/HueRoomsActivity.cpp:170`

**ESP-NOW receive callbacks** (run in the wifi task context, core 0)
- `src/activities/reader/NearbyBookPositionSyncActivity.cpp:846-849`
- `src/activities/network/NearbyStatsSyncActivity.cpp:298-301`
- `freeink-sdk/libs/network/NearbyTransfer/src/NearbyTransfer.cpp:143-149`

**usbd (TinyUSB)**
- `cores/esp32/esp32-hal-tinyusb.c:865`.
- xTaskCreate, 4096, priority configMAX_PRIORITIES-1 = 24, UNPINNED.
- Created only when USB.begin() runs. The call is at `freeink-sdk/libs/hardware/UsbMassStorage/src/UsbMassStorage.cpp:148`, which is USB Drive only.
- Gate: FREEINK_CAP_USB_MSC / CROSSINK_APP_CAP_USB_DRIVE.
- At boot, Serial is HWCDC (ARDUINO_USB_MODE=1). HWCDC creates no task.

**esp_timer**
- Priority 22, CPU0, stack 8192.
- Sources: `framework-espidf/components/esp_timer/src/esp_timer.c:506-509`; `sdkconfig.h:1128-1131`.
- x4-pro keeps 8192 because it does not inherit firmware_tuned's 4096.

**Tmr Svc**
- Priority 1, no affinity, depth 3120 (`sdkconfig.h:1203-1207`).

**ipc0 / ipc1**
- Priority 24, pinned per core, 1024 bytes.
- CONFIG_ESP_IPC_USES_CALLERS_PRIORITY=1.
- Sources: `framework-espidf/components/esp_system/esp_ipc.c:22,27-29,114-115`; `sdkconfig.h:1122-1123`.

**IDLE0 / IDLE1**
- Priority 0, stack 1024 (`sdkconfig.h:1199`).
- The task watchdog checks only the CPU0 idle task (`:1117`). Timeout 5 s, with panic (`:1115-1116`).

## Core 1 contention with the panel-writing task

The panel SPI writers are ActivityManagerRender (core 1, priority 1) and loopTask (core 1, priority 1, under RenderLock).

| Relation to the render task | Tasks |
| --- | --- |
| Same priority on core 1 | loopTask. DictLookup and Tmr Svc are unpinned at priority 1 and can also run on core 1. |
| Higher priority, able to run on core 1 | ipc1 (24, brief); arduino_events (19, pinned to core 1) while the network is up; usbd (24, unpinned) during USB Drive |
| Core 0 only | wifi, tcpip, sys_evt, esp_timer, async_udp, mdns. They interact with core 1 only through shared locks. |

## Locks shared by the render and input paths

### 1. RenderLock

- It is the FreeRTOS mutex `renderingMutex` (`src/activities/ActivityManager.h:112,130`; `ActivityManager.cpp:1375-1392`).
- The render task takes it with portMAX_DELAY (`:481`).
- ScalableFontAccess also uses this mutex (`ActivityManager.cpp:454`; `lib/ScalableFont/HalScalableFont.cpp:334`).

**Try mode (no wait) in loopTask:**
- Battery calibration (`main.cpp:1686`).
- End-of-loop scheduling (`main.cpp:1962-1966`). On failure it calls delay(inputPollDelayMs) and returns.

**Blocking takes in loopTask:**
- Activity push/pop/replace (`ActivityManager.cpp:586, 654`)
- Backdrop restore (`:740`, `:754`)
- Sleep (`:1058`)
- requestManualReaderRefresh (`:1183`)
- `main.cpp:587/600/636/644/695/808/1776`
- Inside activity `loop()` bodies: 192 `RenderLock lock` sites across 140 files under `src/activities`

### 2. HalPowerManager modeMutex

- The render task takes it via HalPowerManager::Lock (`lib/hal/HalPowerManager.cpp:313-336`). The constructor calls setPowerSaving(false), which may call setCpuFrequencyMhz.
- loopTask takes it in setPowerSaving (`HalPowerManager.cpp:65-100`) on every loop.
- Hold times are short, but a CPU frequency switch can happen while it is held.

### 3. Arduino Wire bus lock

- The lock is defined at `~/.platformio/packages/framework-arduinoespressif32/libraries/Wire/src/Wire.cpp:118-120`. CONFIG_DISABLE_HAL_LOCKS is not set, so the lock is active.
- On X4 Pro, the GT911 touch, the RTC and the CW2017 gauge share one I2C bus, SDA39/SCL38. Touch is at `BoardConfig.h:1663-1689`. The gauge comment follows `:1718`.
- loopTask reads touch over this bus. The touch I2C timeout is 10 ms (`freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:1518`).
- The render task reads the gauge through `src/components/themes/BaseTheme.cpp:90,110,928,942`. Those calls reach getBatteryPercentage, which is rate-limited to once per 1500 ms (`HalPowerManager.cpp:266-280`).
- The comment at `main.cpp:1681` says the render task also reads the clock. The RTC call site is UNVERIFIED.

### 4. Task notifications

These are the only render-path signal on input paths. requestUpdate and requestUpdateAndWait notify the render task. loopTask waits in ulTaskNotifyTake (`ActivityManager.cpp:1368-1369`). The render task wakes the waiter (`:494-502`).

## Which sdkconfig x4-pro uses

**The project file is generated and stale.**
- `/Users/bzatrok/dev/CrossInk/sdkconfig.defaults` is generated and gitignored (`.gitignore:51`).
- Its header reads `# TASMOTA__1c717e60840792d9 ... ESP-IDF 5.5.2`.
- It comes from a firmware_tuned S3 build. Evidence: it contains CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON=y (`:483`), a firmware_tuned option.
- Its values:
  - CONFIG_ARDUINO_RUNNING_CORE=1 (`:614`)
  - CONFIG_ARDUINO_LOOP_STACK_SIZE=8192 (`:615`)
  - CONFIG_ARDUINO_EVENT_RUNNING_CORE=1 (`:619`)
  - CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y (`:2351`)
  - CONFIG_ESP_MAIN_TASK_AFFINITY=0x0 (`:2354`)

**x4-pro does not consume it.**
- `[env:x4-pro]` extends base, not firmware_tuned. It has no custom_sdkconfig and no custom_component_remove (`platformio.ini:411-450`; comment at `:128-132`).
- The builder hybrid-compiles only when the env has custom_sdkconfig (`~/.platformio/platforms/espressif32/builder/frameworks/arduino.py:551-556`).
- No root `framework-arduinoespressif32-libs/sdkconfig` exists, so flag_any_custom_sdkconfig is false (`:571-572`).
- The libs are the prebuilt release. Files are dated Feb 11 2026, and the package was reinstalled Oct 5 21:35.
- Side effect: building sticky or default and then x4-pro triggers a framework reinstall (`:646-648`, `:886-905`).
- No script in `scripts/` references sdkconfig. platformio.ini sets neither board_build.sdkconfig nor sdkconfig.defaults.
- x4-pro sets `board_build.arduino.memory_type = dio_opi` (`platformio.ini:423`). The include path then selects `esp32s3/<memory_type>/include` (`esp32s3/pioarduino-build.py:538,545`).

## Effective x4-pro config

Values from `esp32s3/dio_opi/include/sdkconfig.h`.

| Setting | Value | Line |
| --- | --- | --- |
| CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ | 240 | `:1078` |
| CONFIG_SPIRAM | 1 | `:1061` |
| CONFIG_SPIRAM_MODE_OCT | 1 | `:1062` |
| CONFIG_SPIRAM_SPEED_80M | 1 | `:1066` |
| CONFIG_SPIRAM_SPEED | 80 | `:1067` |
| CONFIG_SPIRAM_USE_MALLOC | 1 | `:1072` |
| CONFIG_COMPILER_OPTIMIZATION_SIZE | 1 | `:766` |
| CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE | 1 | `:767` |
| CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL | 2 | `:770` |
| CONFIG_FREERTOS_HZ | 1000 | `:1196` |
| configUSE_TIME_SLICING | 1 | `FreeRTOSConfig.h:94` |
| CONFIG_ARDUINO_RUNNING_CORE | 1 | `:483` |
| CONFIG_ARDUINO_LOOP_STACK_SIZE | 8192, overridden to 16384 at runtime | `:484` |
| CONFIG_ARDUINO_EVENT_RUNNING_CORE | 1 | `:486` |
| CONFIG_ESP_MAIN_TASK_AFFINITY | 0x0 | `:1101` |
| CONFIG_SPI_MASTER_IN_IRAM | Not defined. Generic `esp32s3/sdkconfig:1927` says "is not set". | n/a |
| CONFIG_SPI_MASTER_ISR_IN_IRAM | Not defined. Generic `esp32s3/sdkconfig:1928` says "is not set". | n/a |
| CONFIG_ESPTOOLPY_FLASHMODE | "dio" | `:461` |
| CONFIG_ESPTOOLPY_FLASHFREQ | "80m" | `:463` |
| CONFIG_ESP32S3_DATA_CACHE_SIZE | 0x8000 | `:1086` |
| CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE | 0x4000 | `:1080` |
| CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE | 32 | `:1090` |
| CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE | 32 | `:1084` |
| CONFIG_PM_ENABLE | Not set | `esp32s3/sdkconfig:2226` |

- **Inference, not verified:** Arduino SPI uses esp32-hal-spi rather than the IDF spi_master driver. If so, the two SPI_MASTER IRAM options may not affect the panel path.
- The generic top-level `esp32s3/sdkconfig:2240` says SPIRAM_MODE_QUAD. x4-pro compiles against the dio_opi header, which says OCT.

## Versions

| Component | Version | Source |
| --- | --- | --- |
| Arduino-ESP32 core | 3.3.7 | `~/.platformio/packages/framework-arduinoespressif32/package.json:3` |
| Libs package | "5.5.0+sha.87912cd291" | `framework-arduinoespressif32-libs/package.json:15` |
| esp-idf (libs build) | release/v5.5 87912cd291 | `esp32s3/versions.txt` |
| arduino (libs build) | idf-release/v5.5 86c2c0046 | `esp32s3/versions.txt` |
| tinyusb | master 2883403ed | `esp32s3/versions.txt` |
| pioarduino platform | 55.03.37 | `~/.platformio/platforms/espressif32/platform.json:21` |
| Core pinned by platform | 3.3.7 | `platform.json:36,42` |
| esp-idf pinned by platform | v5.5.2 | `platform.json:60` |
| Toolchain | xtensa-esp-elf 14.2.0_20251107 | `platform.json:67` |
| framework-espidf package | 3.50502 | `framework-espidf/package.json:3` |

## Compiler flags

- App and library CCFLAGS include `-Os` (`esp32s3/pioarduino-build.py:109`).
- `esp32s3/flags/c_flags` and `cpp_flags` add:
  - `-mlongcalls`
  - `-mdisable-hardware-atomics`
  - `-fstack-protector`
  - `-freorder-blocks`
  - `-fno-jump-tables`
  - `-fno-tree-switch-conversion`
- platformio.ini base unflags `-fexceptions` and adds `-fno-exceptions` (`platformio.ini:73-77`).
- No `compile_commands.json` or idedata exists in `.pio/build/x4-pro`. Exact per-file command lines are UNVERIFIED.

## UNVERIFIED

- WiFi task priority. It lives in the closed libnet80211.
- The app caller of AsyncUDP::listen. The symbol is linked.
- The render-task RTC read call site.
- Exact per-file compile command lines.
