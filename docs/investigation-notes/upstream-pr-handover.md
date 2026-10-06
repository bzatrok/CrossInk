# Handover: upstream PRs for the X4 Pro tap latency fixes

Written 2026-10-06 from the fork at `bzatrok/CrossInk`, branch
`perf/x4-pro-fewer-refreshes`. Two user-facing fixes from the latency investigation
are upstream material. Everything else on the branch (trace build, `LATENCY_LOG`,
capture script, `INVESTIGATION.md`, the logs under `docs/investigation-notes/`) stays
in the fork.

## What goes upstream

| PR | Repository | Fork commit | Change |
| --- | --- | --- | --- |
| A | `Free-Ink/freeink-sdk` | `c2ae821` (on `bzatrok/freeink-sdk`, branch `feat/restore-visible-frame`) | GT911 path: suppress the screen contact while the capacitive home key is down (`libs/hardware/InputManager/src/InputManager.cpp`, end of `pollGt911`, 4 lines). |
| B | `uxjulia/CrossInk` | `79c4d8b2` | `HomeActivity.cpp`: touch-down on a cover or menu item no longer repaints; the Classic/Lyra/Lyra Extended/Rounded render path no longer requests a second render after the first paint. Plus the `CHANGELOG.md` lines. |
| C | `uxjulia/CrossInk` | none yet | Submodule pointer bump to the SDK commit that lands from A. Only after A merges. |

Checked 2026-10-06: every touched hunk is byte-identical on `upstream/main` of both
repositories (CrossInk upstream at 65 commits behind the fork, SDK gitlink
`6993701` on `Free-Ink/freeink-sdk` main). The fork's SDK branch is 5 ahead and 13
behind SDK main, so cherry-pick, do not merge the branch.

## Why (for the PR descriptions)

Measured on an Xteink X4 Pro (UC8279 panel) with a serial trace build. One e-ink
refresh costs about 650-700 ms from the finger lift to the panel, 483 ms of which is
the panel waveform. The slow interactions were the ones that triggered two refreshes.

- **Home key presses needed repeating.** The GT911 reports a finger that is on the
  capacitive key and on the glass above it as a screen contact, sometimes with the key
  bit, sometimes without. The contact routed as a tap next to the key instead of Home.
  In one test session: 2, 3, 1 and 2 presses needed. With the SDK fix: four presses,
  all first time. The CST816S cover-key path already did this.
- **Home tiles: 1.5 s to open Settings.** Touch-down moved the selector and repainted,
  a full refresh before the tap's own screen could render. After: 0.7 s.
- **Home key to Home: 1.7 s.** `HomeActivity::render()` always requested a second
  render after the first paint so `loadRecentCovers()` could run afterwards; with cached
  covers that repainted an identical screen. After: 1.1 s, of which 300 ms is the
  X4 Pro double-tap deferral.

Raw logs: `docs/investigation-notes/x4-pro-trace-2026-10-06-eventlines.log` (before)
and `x4-pro-trace-2026-10-06-verified.log` (after). Fork only; quote the numbers.

## State on 2026-10-06 evening: both branches already exist locally

Prepared in this session, nothing pushed, no PR opened:

- `~/dev/freeink-sdk-pr`: git worktree of the SDK, branch `fix/gt911-home-key-precedence`
  from `Free-Ink/freeink-sdk` `upstream/main`, commit `a0eaaee` (cherry-pick of `c2ae821`,
  one file, +5 lines, trailers removed). Not yet built.
- `~/dev/CrossInk-pr`: git worktree of CrossInk, branch `perf/home-one-refresh-per-tap`
  from `uxjulia/CrossInk` `upstream/main`, commit `ecd02955`. The three `LATENCY_LOG`
  lines and the include are already stripped; `CHANGELOG.md` has a new `[Unreleased]`
  section with the two Home entries. Submodules not yet initialised, not yet built.

A new session executing PR A starts at "Build check" below. A session executing PR B
starts at `git submodule update --init --recursive` in `~/dev/CrossInk-pr`, then the
build commands. Remove the worktrees afterwards with `git worktree remove <path>`.

## Review before sending (Ben: heavy manual review, surgical, no AI footprint)

Both PRs go out under Ben's name after he has read every line. Checklist:

- **Diff is only the fix.** `git diff upstream/main --stat` shows two files for B
  (`HomeActivity.cpp`, `CHANGELOG.md`) and one for A. No trace code, no reformatting
  of untouched lines, no stray whitespace.
- **Comments read like their neighbours.** Upstream comments in these files are one to
  three short lines about intent, no measurements, no device names. The two comments
  in B were trimmed to that; drop them entirely if they still stand out.
- **Commit messages match upstream's style.** Upstream subjects are lowercase
  `fix:`/`feat:`/`chore:` plus a short clause, bodies are a few plain sentences or
  none. Both prepared commits follow that and carry no `Co-Authored-By` or session
  trailer. Do not add them when amending. `git log -1 --format=%B | grep -c Claude`
  must print 0.
- **PR description is short and in Ben's words.** Problem, cause, fix, how it was
  tested on the device (the numbers from the Why section are fine), nothing more.
  No generated-with footer, no links to sessions.
- **Changelog lines read like the existing ones.** Compare with the `[Unreleased]`
  block of an earlier upstream release before keeping the wording.
- **Hardware re-test on the PR build**, not on the fork build: run the Verification
  steps on firmware built from the PR worktree.
- **Builds**: `x4-pro`, `default`, `sticky`, and the simulator smoke test for B;
  `x4-pro` from the parent repo for A.

## Where to run what

| PR | Folder | Branch |
| --- | --- | --- |
| A, SDK fix | `~/dev/freeink-sdk-pr` | `fix/gt911-home-key-precedence` |
| B, Home refresh | `~/dev/CrossInk-pr` | `perf/home-one-refresh-per-tap` |
| C, pointer bump | `~/dev/CrossInk-pr` after B is merged, or a fresh worktree from `upstream/main` | new branch |

The fork itself, `~/dev/CrossInk` on `perf/x4-pro-fewer-refreshes`, is not touched by
either PR. The SDK worktree shares the git database with `~/dev/CrossInk/freeink-sdk`.

## Steps

### A. SDK PR

```sh
cd ~/dev/CrossInk/freeink-sdk
git remote add upstream https://github.com/Free-Ink/freeink-sdk.git   # already added
git fetch upstream main
git worktree add ../freeink-sdk-pr -b fix/gt911-home-key-precedence upstream/main
cd ../freeink-sdk-pr
git cherry-pick c2ae821
git push -u origin fix/gt911-home-key-precedence
gh pr create --repo Free-Ink/freeink-sdk --base main
```

Build check from the parent repo with the submodule pointed at the PR commit:
`~/.platformio/penv/bin/pio run -e x4-pro`. Hardware check: see Verification.

### B. CrossInk PR (independent of A)

```sh
cd ~/dev/CrossInk
git fetch upstream
git worktree add ../CrossInk-pr -b perf/home-one-refresh-per-tap upstream/main
cd ../CrossInk-pr
git submodule update --init --recursive
git cherry-pick 79c4d8b2
```

The cherry-pick brings three `LATENCY_LOG(...)` lines into the cover grid branch of
`HomeActivity::render()` (search `LATENCY_LOG` in `src/activities/home/HomeActivity.cpp`).
Upstream has no `util/LatencyTrace.h`, so delete those three lines and the
`#include "util/LatencyTrace.h"` before building. Keep the `CHANGELOG.md` lines under
`[Unreleased]` only; drop the fork-only entries if the cherry-pick drags any in.

Then:

```sh
~/.platformio/penv/bin/pio run -e x4-pro -e default -e sticky
# simulator needs the LLVM 20 recipe in docs/development/fork-workflow.md
python3 scripts/run_simulator_smoke_test.py --env x4-pro-simulator
git push -u origin perf/home-one-refresh-per-tap
gh pr create --repo uxjulia/CrossInk --base main
```

### C. Pointer bump

After A merges: in a branch from `upstream/main`, `git -C freeink-sdk checkout <merged sha>`,
commit the gitlink, PR. Mention PR A in the description.

## Verification on hardware (both PRs)

1. Flash `x4-pro`. Set the UI theme to Lyra or Lyra Extended.
2. Open Settings, wait five seconds, press the Home key once. Open Library, wait five
   seconds, press Home once. Repeat. Expected: four Home presses, all first time.
3. On Home, tap the Settings tile. Expected: one refresh, Settings visible in under a
   second, no pressed highlight before it.
4. Press Home from Settings. Expected: Home appears once; no second identical refresh.
5. Delete a cover thumbnail under `/.crosspoint/` on the SD card and press Home.
   Expected: the "Loading" popup, then one more refresh with the regenerated cover
   (this is the path that still legitimately re-renders).

## Risks and scope notes

- Touch-down no longer repaints the selector on Home. A finger that lands on a cover
  and slides away leaves the selector moved without a repaint until the next render.
  Button navigation starts from that item. Intended trade; the SDK's own FreeInkUI
  screens already paint tap feedback only in the result frame.
- Only the Classic/Lyra/Rounded render branch lost its unconditional second render.
  Minimal, Dashboard, Cover Grid and Carousel branches are untouched and keep theirs.
- The SDK change suppresses the contact for the whole time the key is down. A deliberate
  two-finger gesture that starts while holding the home key is also suppressed; no
  such gesture exists today.
- ESP32-C3 devices have no home key or touch, so the SDK change is inert there; the
  Home change applies to the Sticky and other touch boards too, same reasoning.

## Not upstream, still open in the fork

- Taps held past 500 ms on FreeInkUI screens are swallowed as a long press
  (`INVESTIGATION.md` section 4b). Fix candidates listed there. Worth its own PR later.
- The 300 ms Home key single-tap deferral (`X4PRO_HOME_KEY_DOUBLE_TAP_MS`) is only
  needed when a double-tap action is configured.
- Panel waveform (483 ms of every refresh) is the remaining lever.
