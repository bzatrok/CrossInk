# Handover Log — Tailnet client for the X4 Pro

## Destination
The X4 Pro joins Ben's tailnet through a small Tailscale client of its own, published as a public library.
Any HTTP request to a tailnet address (Dashboard, OPDS, KOReader sync) reaches its server from any Wi-Fi,
at home or travelling. A dashboard wake with a fresh cache adds at most 1 s.

## Intent, verbatim
> i want to make it possible to add the xteink ot the tailnet so i can refresh my dashboard from a tailnet IP instead of a local/public one

> How about we built and extremely lightweight and efficient tailscale client for esp32?

> Agreed, since addresses are fixed and we just enter the ip by hand.
> Device travelling with me
> Do basics, then.

> Q4 I’d say tail net can be also used for odrs and koreaser so could be global?
> Q5 separate repo for lib, client can be in this repo? I’d want this to be GitHub public library ideally (no Claude attribution)

## Invariants
- The project invariants in [PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md) hold. Above all: fork code compiles only
  into the `x4-pro*` environments, behind a capability flag, and reading is never slowed.
- The dashboard invariants in [handover_log.md](handover_log.md) hold. Above all: a failure never keeps the device awake.
- The library has no Arduino and no CrossInk dependency. It compiles natively on macOS.
- The library repo carries no AI attribution: no `Co-Authored-By`, no `Claude-Session`, no "Generated with" line.
- Nothing is made public, and no Tailscale admin setting is changed, without Ben doing it himself.
- Wi-Fi off means no tunnel and no tailnet RAM. Reading with Wi-Fi off is unchanged.

## Notes
- Read `AGENTS.md` (via `CLAUDE.md`), `.claude/CONTEXT.md` and `PROJECT_CONTEXT.md` first. `pio` is at
  `~/.platformio/penv/bin/pio`.
- Reference code: `github.com/CamM2325/microlink` (MIT). Its C code may be reused with its copyright notice kept.
  Reference only, never vendored whole: `github.com/tailscale/tailscale` (BSD-3), for the protocol.

## Decisions so far
<!-- index only: one line per decision, linking the handover that holds the detail -->
- A new client splits a rare slow path (control server) from a per-wake fast path that runs from cached keys and
  peer data. Rejected: MicroLink as-is (about 15-20 s per cold boot, about 116 KB static SRAM, always-on tasks).
  This replaces the dashboard log's earlier rejection of a Tailscale client on the device.
- Terminus moves from the MacBook to AMB-HM-001, the home VM, deployed from `~/prod/Amberglass/Amberglass.Infra`.
  Rejected: staying on the MacBook (a laptop sleeps and travels).
- Ben creates a reusable, pre-authorized auth key tagged `tag:xteink` and an ACL rule for it. Rejected: the
  executor touching the Tailscale admin console.
- The device's private keys live in NVS. Rejected: the SD card (a lost card would expose the node).
- The tunnel is global: every connection to 100.64.0.0/10 goes through it, so Dashboard, OPDS and KOReader sync
  need no change. The only new setting is the auth key. Rejected: a separate on/off switch, and a dashboard-only tunnel.
- The library lives in its own public GitHub repo. The CrossInk integration lives in this repo. Rejected: `lib/` in
  this repo.
- The library is `bzatrok/tailesp32`, MIT, with MicroLink's notice kept for reused code. The executor creates it
  private. Ben makes it public. Rejected: `tinytail` (taken), any name with "tailscale" in it (trademark).
- During development CrossInk pulls `tailesp32` as a root submodule through `symlink://` in the `x4-pro*`
  environments, as it does `freeink-sdk`. Rejected: a `lib_deps` git URL (no editing in place), a vendored copy.
- AMB-HM-001 (tailnet name `amb-biz-host`, `100.85.148.88`, untagged) has no public ingress and the home router offers no port mapping (`tailscale netcheck`, 2026-10-07).
  A travelling device reaches it only through DERP. DERP is required, not a fallback.
- Terminus moves with a `pg_dump`/restore of its database, so device #6, its model and screens carry over.
  The work is a handover in `Amberglass.Infra`. Rejected: a fresh install plus re-provisioning.
- Order: library and Mac prototype, device integration, DERP. Moving Terminus to the home VM is lower priority
  and runs whenever. Rejected: DERP first, and the move as a prerequisite. Ben: "The bigger prop is xteink being
  able to get data from the tailnet address. The migrations are lower prio."
- `CLAUDE.md` and `PROJECT_CONTEXT.md` for `tailesp32` exist only on Ben's Mac, excluded through
  `.git/info/exclude`. Rejected: committing them to the public repo.
- The character feed stays on the Mac. Terminus on the VM fetches it over the tailnet. Rejected: moving the feed
  and its Garmin and Composio credentials to the VM.
- The device joins the `bzatrok.github` tailnet, where the Amberglass hosts are. Ben moves his Mac to it himself.
  Rejected: `medimarket.hu` (shared with zsolt.zatrok, and the home VM is not in it).
- Terminus on the VM listens on all interfaces at 2300. Its `API_URI` follows the device: the LAN URL until the
  device has the tunnel, then the 100.x URL. Rejected: a tailnet-only bind (no server for the device meanwhile).
- `push-template.sh` gains an optional `TERMINUS_SSH`. Rejected: running the push script on the VM.
- Tailnet policy: untagged devices (all of Ben's) keep full access; `tag:xteink` gets only Terminus and Grimmory
  ports plus ICMP on `amb-biz-host` and the Mac, with save-time tests. Rejected: the default allow-all for the device.
- All tailnet state (keys and the peer cache) lives in NVS. Dashboard state stays on SD. Rejected: the peer cache on SD.
- The tunnel starts when Wi-Fi connects and stops with Wi-Fi. A small task sends keepalives and renews keys.
  Rejected: starting on the first 100.x connection (needs a hook inside lwIP routing).
- The infra handover also binds Grimmory's 6060 to the VM's tailnet IP. Rejected: leaving it loopback-only.
- The auth key is entered in the web portal only. Rejected: on-device entry (a `tskey-` string on the e-ink keyboard).
- A new auth key re-registers the device and replaces its keys. Rejected: a separate "forget" action.
- The tailnet status (registered, 100.x address) shows on the web settings page only. Rejected: a new device screen.
- Control traffic uses plain TCP port 80 with Noise inside and a pinned control-server key, as MicroLink does
  (`ml_coord.c:10-12`, `:230`). Rejected: fetching the key over HTTPS (TLS on the slow path).
- Wake budget: the fast path adds at most 1 s. The slow path takes at most 10 s and runs only on a stale (over 24 h)
  or failed cache.
- [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) — the library is C11 with a platform seam; the core is its own CMake target and a networking include fails
  its build. Rejected: C++ or Arduino, a suppressible lint rule.
- [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) — cJSON over a buffered map response (cap 256 KB), one non-streaming map, pinned control key, protocol 131.
  Rejected: a streaming parser, capability 148.
- [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) — the disco key is permanent; IPv4 only; one held packet per peer during a handshake. Rejected: a new disco
  key per start, IPv6 endpoints, a packet queue.
- [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) — the UDP port is the port's choice (`tse_platform.udp_port`, 0 = 41641); the POSIX port uses 41642.
  Rejected: 41641 on the Mac (its Tailscale client holds it), quitting the Mac's Tailscale to test.
- [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) — endpoints in 172.16/12 and 169.254/16 sort last before the cap of 4; HomeDERP falls back to the legacy
  `DERP` field. Rejected: raising the cap.
- [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) — the handshake initiation is re-sent right after we answer the peer's disco ping, at least 50 ms after the
  previous one. A peer answers only over a path its own ping confirmed, and wireguard-go drops initiations within
  20 ms. Rejected: fast blind retries (no effect), waiting for the 5 s WireGuard retry (blows the 1 s budget).

## Not yet specified
<!-- in-scope questions you can see coming but cannot yet state sharply -->
- Wall clock on the device: WireGuard handshake timestamps come from the wall clock. Before SNTP the peer may drop
  them. Handover 005 should start the tunnel after time sync; how CrossInk knows the time is synced is open.
- When `tailesp32` is stable enough for a first release on the PlatformIO Registry, after which CrossInk pins a
  version instead of the submodule. Not before DERP works on hardware.

## Out of scope
<!-- ruled out of this effort, with the reason. It stays ruled out. -->
- ESP32-C3 (X3/X4) and Sticky builds. The project invariant limits fork features to the X4 Pro.
- MagicDNS. Ben types the 100.x address by hand.
- Inbound connections to the device. Every use is the device fetching from a server.

| # | Slug | Goal | Status | Branch | Created | Completed |
|---|------|------|--------|--------|---------|-----------|
| 004 | [tailesp32-core-direct](handover_004_tailesp32-core-direct.md) | New `tailesp32` repo: portable C core, register and map, cache, disco, WireGuard, Mac prototype pings the home VM | ✅ done ([PR #1](https://github.com/bzatrok/tailesp32/pull/1)) | tailesp32 `feat/core-direct-path` | 2026-10-07 | 2026-10-07 |
| 005 | [tailnet-device-integration](handover_005_tailnet-device-integration.md) | ESP-IDF port and lwIP netif, global tunnel on the X4 Pro, auth key in the web portal, dashboard over 100.x at home | ⬜ pending | `feat/tailnet-client` + tailesp32 `feat/esp-idf-port` | 2026-10-07 | — |
| 006 | [tailesp32-derp](handover_006_tailesp32-derp.md) | DERP relay with path memory, so the dashboard refreshes away from home | ⬜ pending | tailesp32 `feat/derp` + `feat/tailnet-derp` | 2026-10-07 | — |
| — | Amberglass.Infra `terminus-home-vm` (`~/prod/Amberglass/Amberglass.Infra/.handovers/handover_004_terminus-home-vm.md`) | Terminus on AMB-HM-001, Grimmory 6060 on the tailnet, feed on the Mac's tailnet IP. Lower priority, any time | ⬜ pending | `feature/terminus-home-vm` | 2026-10-07 | — |

Status legend: ⬜ pending · 🔄 in-progress · ✅ done
