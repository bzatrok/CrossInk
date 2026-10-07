# Project Context — CrossInk (bzatrok fork)

<!-- The project map. Outlives every effort in .handovers/. -->
<!-- Write standing conditions, not current status. See ~/.claude/skills/project-context. -->

## Destination
Ben's personal fork of uxjulia/CrossInk for his Xteink X4 Pro. It tracks upstream and carries
fork-only features that upstream's `SCOPE.md` excludes.

## Intent, verbatim
<!-- Left blank on purpose. Ben has not yet given a sentence on why the fork exists. -->

## Stakeholders
- Ben — the only user and maintainer of this fork.
- uxjulia/CrossInk (`upstream` remote) — owns the base firmware. The fork pulls from it and never pushes to it.
- Free-Ink/freeink-sdk (`freeink-sdk` submodule) — owns the display, power and board drivers.
  A fork change that needs the SDK goes there as a PR, or into a `bzatrok/freeink-sdk` fork while the PR is open.

## Invariants
- Fork-only features compile only into the `x4-pro*` environments, behind a `CROSSINK_APP_CAP_*` flag.
  Other environments carry no code for them.
- Fork changes add files and use small hook points in upstream files, so `git pull upstream main` stays cheap.
- Nothing is pushed upstream from this fork.
- Reading stays the main job. Fork features never slow book open or page turns.

## Decisions so far
<!-- index only: gist + rejected alternative, linking the ADR or handover log -->
- The pointer to this file lives in `.claude/CONTEXT.md`. Rejected: a line in `AGENTS.md`, because
  `CLAUDE.md` is a symlink to upstream's `AGENTS.md` and every upstream pull would conflict on it.
- Durable lines stay in `AGENTS.md` and are not moved here. Rejected: the usual move, because upstream owns that file.
- Home Control (Hue + tado) is fork-only. Detail: `docs/home-control.md`.

## Not yet specified
<!-- the fog: questions not yet sharp, and work deliberately unfinished with its reason -->

## Out of scope
<!-- ruled out for the project, with the reason. Never graduates. -->
- Pushing fork features upstream. Upstream's `SCOPE.md` excludes active connectivity and app-like features.

## Efforts
- [.handovers/handover_log.md](.handovers/handover_log.md) — TRMNL dashboard sleep mode.
- [.handovers/log_tailnet.md](.handovers/log_tailnet.md) — Tailnet client for the X4 Pro (`tailesp32`).
