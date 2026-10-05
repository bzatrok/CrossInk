# Handover 001 — SDK frame restore after deep sleep

**Branch:** CrossInk `feat/sdk-frame-restore` (create from `main`). SDK `feat/restore-visible-frame` (create from the pinned SHA `699370183fa3a0e33c9cb83a36f701bbb6022095`).
**Author:** session 2026-10-05  ·  **Status:** pending
**Log:** [handover_log.md](handover_log.md)
**Serves:** "fetches and redraws without a visible boot or flash".
**Scope:** Add a public SDK method `bool FreeInkDisplay::restoreVisibleFrame()`. It writes the current framebuffer
into the SSD1677 old-image plane (RED RAM) and tells the driver that the baseline is valid. The first FAST refresh
after a deep-sleep wake is then a clean differential update instead of a HALF refresh. CrossInk's HAL already calls
this method by name, so Quick Resume gains a flash-free wake as soon as the submodule moves. The dashboard work in
[dashboard-timer-wake](handover_002_dashboard-timer-wake.md) depends on it.

## 0. Shared context (read first)
- Read [handover_log.md](handover_log.md), `PROJECT_CONTEXT.md`, `AGENTS.md` and `.claude/CONTEXT.md` first.
- The SDK lives in the `freeink-sdk/` submodule. Its upstream is `https://github.com/Free-Ink/freeink-sdk.git`.
  Upstream `main` has moved past the pinned SHA. Do not pull those commits in this handover.
- `bzatrok/freeink-sdk` does not exist yet on GitHub (checked 2026-10-05). `gh` is logged in as `bzatrok`.
- **Claim gate:** OFF in this repo.

| Concern | Path:line |
|---|---|
| Facade members `_bus`, `_driver`, `_redRamSynced`, `_singleBufferFastDiff`, `frameBuffer` | `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h:417,418,471,472,489` |
| `syncRedRamFromFrameBuffer` (template for the new method; dual-buffer branch inverts around the seed) | `freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:745-776` |
| `_inversionDirty` turns FAST into HALF | `FreeInkDisplay.cpp:597-599`, set in `setInverted` `:325-333` |
| Single-buffer `displayBuffer` passes `prev=nullptr` | `FreeInkDisplay.cpp:600-606` |
| `begin()` clears the framebuffer to 0xFF | `FreeInkDisplay.cpp:193-234` (`:227`) |
| SSD1677 `seedPreviousFrame` (RED write only, no refresh, no busy wait) | `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp:607-614` |
| `initController` fills BW and RED with 0xF7 and sets `_needsInitialFull` | `Ssd1677Driver.cpp:228-240` |
| First FAST becomes HALF (0xD7) while `_needsInitialFull` | `Ssd1677Driver.cpp:467-483` |
| `requestResync` sets `_needsGrayClear` | `Ssd1677Driver.h:98`, `Ssd1677Driver.cpp:456-459` |
| Deep sleep mode 2 discards RAM | `Ssd1677Driver.cpp:757-761` |
| `PanelDriver` virtual base (`seedPreviousFrame` default no-op) | `freeink-sdk/libs/display/FreeInkDisplay/src/driver/PanelDriver.h:95-107` |
| Host test file and runner | `freeink-sdk/libs/display/FreeInkDisplay/test/host/test_pro.cpp` (`main` at `:615-643`, pattern `testAsyncFrame` at `:133`), `run_pro.py` |
| CrossInk caller (`requires`-gated) | `lib/hal/HalDisplay.cpp:6-15`, `:90-97` |
| CrossInk wake path that calls it | `src/main.cpp:1452-1491` (call at `:1466`), `setInverted` at `:1162` runs earlier |
| Saved frame file | `src/main.cpp:993` (`/.crosspoint/sleep_frame.bin`), save `:995-1000`, load `:1002-1014` |

## 1 — SDK: add the driver hook  *(DECIDED)*
### Root cause (confirmed)
After deep sleep the SSD1677 RAM is gone (mode 2). `begin()` resets the controller, fills BW and RED with 0xF7,
and sets `_needsInitialFull`. The first FAST refresh is therefore promoted to HALF, which is the visible flash.
`syncRedRamFromFrameBuffer()` pushes nothing in single-buffer mode, which is the mode the X4 Pro uses.
### Fix
- In `PanelDriver.h`, add `virtual bool restoreVisibleFrame(EpdBus& bus, const uint8_t* onScreen) { (void)bus; (void)onScreen; return false; }`.
  Comment it: the caller guarantees `onScreen` is exactly what the glass shows. The default returns false, so
  panels without support keep their normal first refresh.
- In `Ssd1677Driver`, override it: call `seedPreviousFrame(bus, onScreen)`, set `_needsInitialFull = false`,
  return true. Leave `_needsGrayClear` alone, because a caller that asked for a resync still gets one.

## 2 — SDK: add the facade method  *(DECIDED)*
- Declare `bool restoreVisibleFrame();` in `FreeInkDisplay.h` next to `syncRedRamFromFrameBuffer` (`:246`), with a
  comment: call after `begin()` once the framebuffer holds the frame that is on the glass. Returns false when the
  panel cannot restore a baseline.
- Implement in `FreeInkDisplay.cpp` after `syncRedRamFromFrameBuffer`:
  1. Return false if `_driver` is null or `_panelSel == PanelSel::X3`.
  2. `syncPendingAsync()`.
  3. If `_inverted`, invert the framebuffer in place, call the driver hook, invert back. Copy the exact pattern from
     the dual-buffer branch of `syncRedRamFromFrameBuffer` (`:764-774`).
  4. If the hook returns false, return false.
  5. Set `_redRamSynced = true` and `_inversionDirty = false`. In dual-buffer builds also set
     `_redBaselineAuthoritative = false`, as `syncRedRamFromFrameBuffer` does.
  6. Return true.
- Build it for both buffer modes. Do not guard it with `EINK_DISPLAY_SINGLE_BUFFER_MODE`.

## 3 — SDK: host test  *(DECIDED)*
- Add `testRestoreVisibleFrame()` to `test_pro.cpp` and call it from `main`. Follow `testAsyncFrame`.
- Assert three things on the X4 Pro stub board:
  1. After `begin()` plus `restoreVisibleFrame()`, the recorded bus writes contain a RED write (command 0x26) of the framebuffer.
  2. The next `displayBuffer(FAST_REFRESH)` activates with 0xFC, not 0xD7.
  3. With `setInverted(true)` before the call, the RED write holds the inverted bytes and the next FAST is still 0xFC.
- Run from `freeink-sdk/`: `python3 libs/display/FreeInkDisplay/test/host/run_pro.py`. All variants must pass.

## 4 — Publish the SDK branch and move the submodule  *(DECIDED)*
1. `gh repo fork Free-Ink/freeink-sdk --clone=false` (creates `bzatrok/freeink-sdk`).
2. In `freeink-sdk/`: add remote `fork` = `git@github.com:bzatrok/freeink-sdk.git`. Create
   `feat/restore-visible-frame` from the pinned SHA. Commit in two commits (hook + facade, then test). Push to `fork`.
3. In CrossInk: set the `freeink-sdk` URL in `.gitmodules` to `https://github.com/bzatrok/freeink-sdk.git`
   (HTTPS, so clones without SSH keys still work). Run `git submodule sync`. Commit the new gitlink.
4. Do not open the PR to Free-Ink. Write the PR title and body into the CrossInk PR description under
   "Upstream SDK PR (draft)". Ben opens it himself.

## 5 — CrossInk: no code change expected  *(DECIDED)*
- `lib/hal/HalDisplay.cpp:6-15` detects the method with `requires`. Once the submodule moves, the Quick Resume wake
  path at `src/main.cpp:1466` starts using it. Do not edit `main.cpp` in this handover.
- Add one `CHANGELOG.md` line under the unreleased section's "Fixed": X4 Pro Quick Resume wakes with a differential
  refresh instead of a half refresh.
- Add one line to `.claude/CONTEXT.md` under "Fork Tooling": the SDK submodule points at `bzatrok/freeink-sdk`,
  branch `feat/restore-visible-frame`, until Free-Ink merges it.

## Sequencing
1 → 2 → 3 (SDK host tests pass) → 4 → 5 → acceptance. Handover 002 must not start before this merges.

## Decisions — locked ✅
- New `PanelDriver::restoreVisibleFrame` hook that also clears `_needsInitialFull`. Rejected: calling the existing
  `seedPreviousFrame` alone, because the driver still promotes the first FAST to HALF.
- Clear `_inversionDirty` in the facade after seeding the inverted baseline. Rejected: leaving it set, because it
  promotes FAST to HALF a second time.
- Leave `_needsGrayClear` alone. Rejected: clearing it, because it would silently drop an explicit `requestResync()`.
- SDK branch from the pinned SHA. Rejected: from upstream `main`, because it pulls unrelated SDK changes into this fork.
- Submodule URL over HTTPS to the fork. Rejected: SSH URL, because it breaks clones without keys.
- Ben opens the upstream PR. Rejected: the executor opens it, because it is an outward-facing action on someone else's repo.

## Out of scope for this handover
- Any change to `src/main.cpp` wake logic. Handover 002 owns it.
- Pulling newer upstream SDK commits.
- Drivers other than SSD1677. They keep the default `return false`.

## Acceptance / pre-merge checklist
- `python3 libs/display/FreeInkDisplay/test/host/run_pro.py` passes (run in `freeink-sdk/`).
- `~/.platformio/penv/bin/pio run -e x4-pro` builds.
- `~/.platformio/penv/bin/pio run -e default` builds (the C3 build must still compile with the new SDK).
- `git submodule status` shows the new SHA, and `.gitmodules` points at `bzatrok/freeink-sdk`.
- Hardware check for Ben, in the PR body: set Sleep Screen to Quick Resume, open a book, sleep, wake.
  Expected: the page appears with a FAST differential refresh and no black-white flash. Serial log shows no
  HALF promotion on the first paint. If the image ghosts, report it on the PR. Do not tune waveforms here.

---
**On completion:**
1. Update `.handovers/handover_log.md`. Set this handover's status to ✅ done and fill in the Completed date. If you
   could not finish, set 🔄 in-progress and add a note row that describes what remains.
2. Add each locked decision to the log's **Decisions so far**. Write one line: the gist plus the rejected
   alternative, linking this doc by name. The detail stays here.
3. Move anything this work made sharp out of **Not yet specified**. Move anything it ruled out into **Out of scope**.
4. Commit, push the branch, and open the PR (to `bzatrok/CrossInk`, base `main`).
