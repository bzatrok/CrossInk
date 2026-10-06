# X4 Pro input pipeline: hardware to panel push

Buttons and the GT911 are both polled from the Arduino loop task. There is no interrupt and no input queue. The reader's page turn fires on button release or on touch lift.

All paths are relative to /Users/bzatrok/dev/CrossInk. The SDK submodule is pinned at 918136bf (freeink-sdk). Framework facts come from the locally installed ~/.platformio framework-arduinoespressif32 package. They are not checked against the exact pioarduino 55.03.37 pin.

## 1. Button hardware read
- **Profile.**
  - freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1629 defines XTEINK_X4_PRO with InputStyle::DigitalButtons (:1632).
  - Pins {back,confirm,left,right,up,down,power} are set at :1664. Only up=GPIO0, down=GPIO7 and power=GPIO3 are assigned, all active-LOW.
  - The two physical keys are BTN_UP and BTN_DOWN, which the app calls "side" buttons. Back and confirm come from the GT911 and its capacitive Home key.
- **Polled, no GPIO interrupt.**
  - begin() sets INPUT_PULLUP at freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:119-136.
  - getDigitalState() reads the pins with digitalRead at :338-367.
  - getState() calls it at :190-195, and update() calls getState() at :526-566.
- **Debounce.**
  - DEBOUNCE_DELAY = 5 ms (include/InputManager.h:454).
  - In InputManager.cpp:555-566, a raw change sets lastDebounceTime. The change commits only on a later update() call where (now - lastDebounceTime) > 5. A press therefore needs two polls more than 5 ms apart.
- **App compensation.**
  - lib/hal/HalGPIO.cpp:186-195 calls inputMgr.update(). If isDebouncePending() is true, it runs delay(BUTTON_DEBOUNCE_REPOLL_MS = 6, HalGPIO.cpp:32) and calls update() again.
  - Net cost: a 6 ms blocking delay in the loop task on every raw button edge, press and release.
- **Hold and long-press timers.**
  - The SDK hold timers apply to other input styles only, not DigitalButtons: CONFIRM_BACK_HOLD_MS 650, CONFIRM_POWER_HOLD_MS 400, TWO_BUTTON_HOLD_MS 650 (InputManager.h:455-457).
  - App side: ReaderUtils::SKIP_HOLD_MS = 700 and GO_HOME_MS = 1000 (src/activities/reader/ReaderUtils.h:20-21).
  - No repeat timers were found.
- **Where the poll runs.**
  - No SDK task runs it. beginAsync() (InputManager.cpp:225-273) would start a 15 ms "fi_input" task with queues, but nothing calls it. rg beginAsync over src/, lib/ and include/ returns zero hits.
  - update() runs only from the loop task: main.cpp:1698 mappedInputManager.update() -> src/MappedInputManager.cpp:116-117 gpio.update() -> HalGPIO.cpp:187.

## 2. Touch hardware read (GT911)
- **Profile.** BoardConfig.h:1679-1700.
  - SDA=39, SCL=38, IRQ=10, RST=4. Address 0x5D, alt 0x14.
  - powerEnable is GPIO2, active-LOW. The profile sets swapXY, flipY and the Home key.
  - The I2C bus is shared with the BM8563 RTC at 0x51 and the CW2017 gauge at 0x63 (:1676-1678, :1719, :1724).
- **Init.** beginGt911() is at InputManager.cpp:1903-1972.
  - Wire.begin(sda, scl, 400000) and Wire.setTimeOut(10) at :1922-1923.
  - The IRQ pin is used only as the address strap in the reset dance, then set to INPUT (:1927-1938). Nothing calls attachInterrupt for the GT911.
  - Boot delays: 50 ms for the power rail, then 10+10+50+50 ms per reset attempt.
- **Polling.**
  - serviceTouch() (:1264-1309) is called from getState() :192 on every update(). It dispatches to pollGt911() (:2243-2373).
  - The GT911 path has no rate limit. TOUCH_SAMPLE_DELAY_MS = 8 (InputManager.h:462) applies only to the CHSC6x path (:1321-1322). The I2C cadence therefore equals the loop cadence.
- **What one poll does.**
  1. Read the 0x814E status register, 1 byte (:2248).
  2. If bit 0x80 (buffer ready) is clear, return (:2266).
  3. Otherwise read min(count,4)*8 bytes at 0x8150 in one transaction (:2290).
  4. Write 0x814E=0 to clear the status (:2372, :1992-1998).
- The GT911's internal report rate is UNVERIFIED, because the controller self-loads its config.
- **Gesture logic.** All of it runs on the loop task.
  - The press edge is the first frame with count>0 (:2322-2328).
  - The release edge needs a fresh frame with count==0 (:2357-2366). Taps and swipes are classified only on release.
  - Tap: wasTouchTap at :672-691. Valid on release unless movement exceeded TOUCH_TAP_RELEASE_SLOP_PX = 59. It routes to the touch-down point.
  - Swipe: wasSwipe at :749-770. Requires hold time of at most TOUCH_SWIPE_MAX_MS = 700 and travel of at least TOUCH_SWIPE_MIN_PX = 60 on either axis.
  - Stationary slop TOUCH_TAP_SLOP_PX = 28. Long press TOUCH_LONG_PRESS_MS = 500 (:1300-1304). Home-key long press HOME_KEY_LONG_PRESS_MS = 700.
  - These constants are at InputManager.h:411 and :460-470.
- **Wire locking.**
  - Arduino Wire takes a per-bus mutex with portMAX_DELAY in beginTransmission and requestFrom (framework libraries/Wire/src/Wire.cpp:419-427, :494-502).
  - A gauge or RTC read on the render task can therefore block the GT911 poll on the loop task. The size of that wait is UNVERIFIED.
  - Gauge reads are cached for BATTERY_POLL_MS = 1500 (lib/hal/HalPowerManager.h:44, .cpp:270).

## 3. App-side chain
- **Main loop.** main.cpp loop() at :1675.
  - :1698 mappedInputManager.update().
  - :1746-1758 sets userInputReceived, then calls setPowerSaving(false) and notifyUserInput().
  - :1777 runs the updateUpDown chord check. It passes through unless both side keys are held (src/util/ButtonShortcutController.h:92-114).
  - :1866 handleX4ProHomeKeyShortcuts, which handles the Home key only.
  - :1933 activityManager.loop().
- **ActivityManager::loop()** at src/activities/ActivityManager.cpp:505.
  - Touch filters run before the activity: applyLiveTwoFingerLightSwipe :283, applyTwoFingerRotation :442, applyTwoFingerSwipeAction :363, applyEdgeSlideAction :381, the light-panel gesture, and handleGlobalHomeGesture :761.
  - Then it calls currentActivity->loop() (:551-553).
- **EpubReaderActivity::loop()** at src/activities/reader/EpubReaderActivity.cpp:2798.
  - Button path:
    1. sideButtonShortcuts.update() at :3182 calls SideButtonShortcuts::updateOne (src/activities/reader/SideButtonShortcuts.h:53-85). The short action fires on RELEASE. The long action fires at a 700 ms hold.
    2. Defaults: Down short = PAGE_TURN, Up short = PREVIOUS_PAGE (src/CrossPointSettings.h:548-551).
    3. The default branch of the switch (:3226-3232) calls handleShortcutAction(SHORT_PWRBTN) at :5212.
    4. That calls requestManualPageTurn(true/false, "shortcut") at :5215-5219.
    5. ReaderUtils::detectPageTurn (ReaderUtils.h:216) covers only front buttons, power and tilt. It is not on the X4 Pro key path.
  - Touch path:
    1. ReaderUtils::detectTouchPageTurn (ReaderUtils.h:125-188) is called at :2833.
    2. It first calls MappedInputManager::wasSwipe() (MappedInputManager.cpp:696 -> decodeSwipe :578 -> HalGPIO::wasSwipe HalGPIO.cpp:286 -> InputManager::wasSwipe).
    3. If there is no swipe, it calls wasScreenTapped (:314 -> HalGPIO::wasTouchTap :268). The tap zones split at width/3.
    4. :3322-3324 resolves prev and next. :3416 calls requestManualPageTurn(!prev, "touch").
- **requestManualPageTurn** at :5825-5855.
  - It queues the turn instead of running it when RenderLock::peek() is true.
  - It also queues when less than MIN_MANUAL_PAGE_TURN_GAP_MS = 200 (:110) has passed since lastPageTurnTime.
  - Otherwise it calls pageTurn().
- **pageTurn** at :5909-5986.
  - It changes section->currentPage. It takes a blocking RenderLock only at a chapter boundary (:5953, :5976).
  - It then sets lastPageTurnTime = millis() at :5984 and calls requestUpdate() at :5985.
- **requestUpdate.** Activity.cpp:13 calls ActivityManager::requestUpdate (:1324-1334), which sets requestedUpdate = true. The request is deferred.
  - The end of ActivityManager::loop consumes the flag (:726-731) with xTaskNotify(renderTaskHandle, 1, eIncrement).
- **Render task.** renderTaskLoop at :476-503.
  1. ulTaskNotifyTake(pdTRUE, portMAX_DELAY) at :478.
  2. Blocking RenderLock at :481.
  3. HalPowerManager::Lock at :486.
  4. currentActivity->render(std::move(lock)) at :489.
- **Reader render.**
  1. EpubReaderActivity::render at :5989.
  2. loadPage at :6700.
  3. renderContents(..., updatePanel=true) at :6735. The function body is at :7244.
  4. For a plain text page, the panel push is ReaderUtils::displayWithRefreshCycle at :7468 (ReaderUtils.h:234-251).
  5. Grayscale pages use displayGrayscaleBase or the async push at :7449-7460, then the tiled grayscale pass at :7471.
- **Display call chain.**
  1. displayWithRefreshCycle calls GfxRenderer::displayBuffer (lib/GfxRenderer/GfxRenderer.cpp:2395).
  2. That calls HalDisplay::displayBuffer (lib/hal/HalDisplay.cpp:75), which takes HalSpiBus::Lock. That lock is a recursive mutex taken with portMAX_DELAY (lib/hal/HalSpiBus.cpp:17-28).
  3. HalDisplay calls FreeInkDisplay::displayBuffer (freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:590).
  4. FreeInkDisplay calls _driver->display.
- **Driver selection.** The driver is chosen at boot by probe: SSD1677, UC8179 or UC8279_X4 (FreeInkDisplay.cpp:146-169). All three are compiled for X4PRO (BoardConfig.h:120-154).
  - SSD1677: CMD_MASTER_ACTIVATION, then bus.waitRefreshComplete (driver/Ssd1677Driver.cpp:390-391).
  - SSD1677 async variant: displayStart, then displayFinish -> waitRefreshComplete (:430-437).
  - The async call does NOT take the HalSpiBus lock (HalDisplay.cpp:99-105).
- RenderLock is held for the whole of render(), including the panel busy-wait. No lock.unlock() was found inside render().

## 4. Task hops
- **Hop A, input to loop.** No hop: the loop task polls GPIO and I2C itself.
  - loopTask runs at priority 1 on core ARDUINO_RUNNING_CORE = 1 (framework cores/esp32/main.cpp:113, sdkconfig CONFIG_ARDUINO_RUNNING_CORE 1).
  - CONFIG_FREERTOS_HZ is 1000, so one tick is 1 ms.
- **Hop B, loop to render.**
  - Mechanism: a direct task notification, xTaskNotify with eIncrement (ActivityManager.cpp:730).
  - The render task takes it with ulTaskNotifyTake(pdTRUE) (:478). pdTRUE clears the count, so several notifications coalesce into one render.
  - The render task runs at priority 1. It is pinned to core 1 when configNUM_CORES > 1 (:457-466). Its stack is READER_RENDER_TASK_STACK_BYTES = 24576 on S3 (main.cpp:376).
  - Both tasks share core 1 at the same priority. The notify does not preempt the loop; the two tasks are time-sliced at the 1 ms tick.
- **Locks.**
  - renderingMutex, used through RenderLock (ActivityManager.cpp:1375-1392). The render task blocks on it. The main loop only uses Try (main.cpp:1686, :1962) or peek() (reader :2864, :5849, :5859).
  - The HalSpiBus recursive mutex. HalDisplay::displayBuffer takes it, and so does every HalStorage call (lib/hal/HalStorage.cpp:219).
  - The Wire mutex for I2C.
- **Hop C, render to panel done.**
  - SSD1677 path: EpdBus::waitRefreshComplete sleeps the render task on the binary semaphore s_epdRefreshDone. The BUSY-pin CHANGE interrupt handler epdBusyIsr gives it (freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp:71-77, :366-399).
  - The interrupt path is used because no busy-wait slice hook is installed. rg for setBusyWaitSliceHook and setBusyWaitHooks in src/ and lib/ returns zero hits.
  - Timeouts are 20 ms for BUSY assertion and 30000 ms for completion.
  - UC8179 and UC8279 use the UcIdleHigh polled path. It runs delay(1), then a digitalRead loop with busyIdle delay(1) (:279-293, :325-328, EpdBus.h:122-129).
- **Polling periods that add latency.**
  - The loop delay is activityManager.inputPollDelayMs(). The Activity default is 10 (Activity.h:59), and EpubReaderActivity does not override it.
  - main.cpp:1965 applies that delay when the render lock is busy, and :1980 applies it when the device is active.
  - After 3000 ms of inactivity (HalPowerManager.h:43) the delay becomes 50 ms (main.cpp:1977). The CPU drops to LOW_POWER_FREQ = 80 MHz, because the x4-pro board defines BOARD_HAS_PSRAM (HalPowerManager.h:38-39).
  - The normal CPU clock is 240 MHz (board json f_cpu). Input restores it (main.cpp:1752-1753).
  - skipLoopDelay() is true only while a section build tick is wanted (EpubReaderActivity.h:518-521). In that case the loop calls yield() instead of delaying.
- **Worst-case added latency from polling.** This is arithmetic on the constants above. It excludes the time the loop body itself takes.
  - Button edge detection: up to 10 ms when active or 50 ms when idle, plus the 6 ms debounce re-poll.
  - Because the turn fires on release, a short press pays that cost twice, once for press and once for release, plus the physical hold time.
  - Render start: at most one tick (1 ms) after the notify, or as soon as the loop reaches its delay.
  - A turn within 200 ms of the previous turn is deferred to a later loop pass.
  - Touch: the release is seen at the first count==0 GT911 frame after the finger lifts, then up to one loop period. The GT911 frame interval is UNVERIFIED.

## 5. Explicit delays on the path
- **main.cpp:**
  - delay(10) or delay(50) in the exclusive-storage branch (:1709, :1714).
  - delay(inputPollDelayMs) at :1965 and :1980.
  - delay(50) when idle at :1977.
  - TILT_SLEEP_RETRY_DELAY_MS = 10 (:828, :839). It is not on the page-turn path.
- **HalGPIO:**
  - delay(6) debounce re-poll at :192.
  - delay(1) loops only in power-wake verification (:323, :333).
  - delay(3) once, in the first probe of usbHostSofActive (:360).
- **InputManager:**
  - No delay in update() or pollGt911.
  - asyncPoll has vTaskDelay(_asyncPollMs), but asyncPoll is unused.
  - Delays appear only in begin paths: :1468, :1752-1804, :1918-1938, :2070-2113.
- **ActivityManager:** none.
- **MappedInputManager:** none.
- **Reader loop and pageTurn:** none on the turn path.
  - Delays of 1000 ms and 1200 ms exist in other reader flows, not the page turn: EpubReaderActivity.cpp:3753, :3804, :3991, :4039, :4050, :4100, :4208, :4251, :4961, :5101.
- **Display:**
  - Ssd1677 delay(10) on soft reset (:204) and delay(200) on power-down (:417).
  - EpdBus waits either poll with delay(1) or delay(10), or sleep on the interrupt semaphore.
- Arduino delay(ms) is vTaskDelay(ms/portTICK_PERIOD_MS) (esp32-hal-misc.c:212-214).

## 6. Does input stall during a refresh?
- **Normal case: no.**
  - The render task holds renderingMutex through the busy-wait.
  - The main loop only tries the lock or peeks at it, then runs delay(inputPollDelayMs) and returns (main.cpp:1962-1967). Polling therefore continues about every 10 ms.
  - A second turn during a refresh is captured. requestManualPageTurn sees RenderLock::peek() and enqueues the turn into ManualPageTurnQueue.
- **Queue rules.**
  - MAX_PENDING_TURNS = 5 (src/activities/reader/ManualPageTurnQueue.h:15).
  - A turn in the opposite direction cancels the queue (:27-33).
  - A reversal of the turn already in flight is kept (:66-71).
  - drainPendingManualPageTurn (:5857-5879) drains the queue. The loop calls it at :3236 and :3352. A queued turn runs once the lock is free and at least 200 ms has passed since the last turn.
- **When the loop task does block.** It blocks whenever it takes a lock with a full wait while the render task owns it:
  - A blocking RenderLock at a chapter boundary in pageTurn (:5953, :5976).
  - A blocking RenderLock in the chapter-skip and long-press paths (:3196, :3275, :3296, :3407).
  - A blocking RenderLock in ActivityManager push, pop and replace (:568, :637).
  - The HalSpiBus mutex, on any HalStorage call made during a blocking displayBuffer.
  - The Wire mutex.
- **When the loop task is busy instead of polling.**
  - idlePrewarmNextPage (:2733-2785) takes the lock with Try, then loads an SD page and scans fonts. It runs only 400 ms after a render, and only for SD fonts (:113).
  - The background section build tick, buildSomeMore(1) (:2921-2945, EpubReaderActivity.h:343).
- **Lost presses.** There is no input queue between polls, because beginAsync is unused. A press and release that both fall inside one stalled loop pass produce no edge and are lost. The SDK header documents this hazard (InputManager.h:217-229).

## 7. Existing timing instrumentation
- millis() is used throughout, but for state, not for latency measurement. Examples: lastPageTurnTime, pageShownAtMs and lastRenderCompleteMs (EpubReaderActivity.cpp:5984, :6748, :6757).
- main.cpp:1945-1952 logs a new maximum loop duration above 50 ms through LOG_DBG.
  - The x4-pro env sets LOG_LEVEL=1, so this log is compiled out (lib/Logging/Logging.h:49-52).
  - x4-pro-debug sets LOG_LEVEL=2, so the log is active there.
- The idle prewarm duration is logged with LOG_DBG (EpubReaderActivity.cpp:2783). It is also debug-only.
- EpdBus prints "[ms] Wait complete: refresh (N ms)" whenever Serial is connected, regardless of LOG_LEVEL (EpdBus.cpp:319-321, :401-403). This is the only refresh timer that runs in release builds.
- Nothing on the input path uses esp_timer_get_time() or micros(). rg across src, lib/hal, lib/GfxRenderer, the SDK InputManager and FreeInkDisplay returns zero hits.

## Ordered chains

### Button: X4 Pro Down key, default PAGE_TURN
1. GPIO7 changes level.
2. loopTask runs loop() (main.cpp:1675).
3. MappedInputManager::update (MappedInputManager.cpp:116).
4. HalGPIO::update (HalGPIO.cpp:186).
5. InputManager::update (InputManager.cpp:526).
6. getState (:164).
7. getDigitalState reads the pin with digitalRead (:338).
8. Debounce (:555-566), plus the HalGPIO delay(6) re-poll (:192).
9. The press edge is recorded. Nothing fires yet.
10. The release edge goes through steps 2-8 again.
11. ActivityManager::loop (ActivityManager.cpp:505).
12. EpubReaderActivity::loop (EpubReaderActivity.cpp:2798).
13. SideButtonShortcuts::update (:3182, SideButtonShortcuts.h:37). It fires on release.
14. EpubReaderActivity::handleShortcutAction(SHORT_PWRBTN) (:5212).
15. requestManualPageTurn (:5825).
16. pageTurn (:5909).
17. Activity::requestUpdate (Activity.cpp:13).
18. ActivityManager::requestUpdate sets the flag (ActivityManager.cpp:1324).
19. The end of ActivityManager::loop calls xTaskNotify (:730).
20. renderTaskLoop wakes from ulTaskNotifyTake (:478).
21. It takes RenderLock (:481).
22. EpubReaderActivity::render (:5989).
23. renderContents (:7244).
24. ReaderUtils::displayWithRefreshCycle (ReaderUtils.h:234).
25. GfxRenderer::displayBuffer (GfxRenderer.cpp:2395).
26. HalDisplay::displayBuffer (HalDisplay.cpp:75).
27. FreeInkDisplay::displayBuffer (FreeInkDisplay.cpp:590).
28. The SSD1677 driver sends MASTER_ACTIVATION (Ssd1677Driver.cpp:390).
29. EpdBus::waitRefreshComplete waits on the interrupt semaphore (EpdBus.cpp:325).

### Touch: tap or swipe
1. The GT911 produces a frame.
2. loopTask runs loop() (main.cpp:1675).
3. MappedInputManager::update.
4. HalGPIO::update.
5. InputManager::update (:526).
6. getState (:190-195).
7. serviceTouch (:1264).
8. pollGt911 (:2243) reads 0x814E and 0x8150 over I2C, then clears the status.
9. The release edge is raised on a count==0 frame (:2357-2366).
10. ActivityManager::loop (:505) runs the two-finger, edge-slide, light-panel and Home-gesture filters (:283, :442, :363, :381, :761).
11. EpubReaderActivity::loop (:2798).
12. ReaderUtils::detectTouchPageTurn (ReaderUtils.h:125).
13. MappedInputManager::wasSwipe (:696) or wasScreenTapped (:314).
14. HalGPIO::wasSwipe (:286) or wasTouchTap (:268).
15. InputManager::wasSwipe (:749) or wasTouchTap (:672).
16. requestManualPageTurn(…, "touch") (:3416 -> :5825).
17. pageTurn (:5909).
18. From requestUpdate onward, the chain is identical to the button chain, steps 17-29.

## UNVERIFIED
- The GT911 report interval.
- The duration of one I2C transaction at 400 kHz.
- How long the setCpuFrequencyMhz switch from 80 to 240 MHz takes.
- How long the loop can wait on the Wire mutex.
- Which display controller a given unit is detected as at boot.
- Whether the installed framework package matches the pinned pioarduino release.
