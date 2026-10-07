# Handover 006 — DERP relay, so the tailnet works away from home

**Branch:** `tailesp32` `feat/derp` (create from `main`). CrossInk `feat/tailnet-derp` (create from `main` after
[tailnet-device-integration](handover_005_tailnet-device-integration.md) merges).  ·  **Author:** session
2026-10-07  ·  **Status:** pending
**Log:** [log_tailnet.md](log_tailnet.md)
**Serves:** "from any Wi-Fi, at home or travelling". The home VM has no public ingress and the home router offers
no port mapping, so a travelling device reaches it only through a DERP relay (log, Decisions so far).
**Scope:** Add a DERP client to `tailesp32`: TLS to a Tailscale DERP server, the DERP frame protocol, disco and
WireGuard carried over it. Pick our home DERP region, report it to control, and fall back from the direct path
to DERP. Remember which path worked per network. Bump the submodule in CrossInk and prove a dashboard refresh over
a phone hotspot.

## 0. Shared context (read first)

- Read [log_tailnet.md](log_tailnet.md), [PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md), `CLAUDE.md` (→ `AGENTS.md`)
  and `.claude/CONTEXT.md`.
- **Prerequisite:** [tailnet-device-integration](handover_005_tailnet-device-integration.md) is ✅ in the log. Stop if not.
- **Two repos, two rules.** CrossInk commits keep the normal attribution trailers. `tailesp32` commits and its PR
  follow the no-AI-footprint Invariant: no trailers, no "Generated with", no handover number in titles.
- Clone the references into your scratchpad with `git clone --depth 1`: `github.com/CamM2325/microlink` (MIT) and
  `github.com/tailscale/tailscale` (BSD-3, reference only).

  | Concern | Path |
  |---|---|
  | Frame types: ServerKey 0x01, ClientInfo 0x02, ServerInfo 0x03, SendPacket 0x04, RecvPacket 0x05, KeepAlive 0x06, Ping 0x12, Pong 0x13 | tailscale `derp/derp.go:72-78`, `:118-119` |
  | HTTP Upgrade `GET /derp`, `Upgrade: DERP` | tailscale `derp/derphttp/derphttp_client.go:294`, `:512` |
  | Client handshake and frame loop | tailscale `derp/derp_client.go` |
  | C reference: TLS bio, connect, upgrade, send and receive | microlink `components/microlink/src/ml_derp.c:53-235`, `:641-770` |
  | `PreferredDERP` inside `Hostinfo.NetInfo` | microlink `components/microlink/src/ml_coord.c:721-727` |
  | Peer home region field `HomeDERP` | tailscale `tailcfg/tailcfg.go:418-423` |
  | WireGuard packets from DERP are accepted (`DerpMagicIPAddr`) | tailscale `wgengine/magicsock/magicsock.go:2564` |

- **Claim gate:** ON, registered globally in `~/.claude/settings.json` (a Stop hook running
  `~/.claude/hooks/review-gate.sh`). On every turn that changes a file, a fresh reviewer compares your closing
  message against the diff. A contradicted claim blocks the turn and the reason comes back on stderr. Full reviews
  land under `~/.claude/reviews/`. The gate releases itself after 3 blocks in a row. When blocked, correct the work
  or correct the claim. Never work around the gate. The acceptance checklist is the set of claims your final turn
  is judged against.

## 1 — TLS in the platform seam  *(DECIDED)*

- `src/platform.h` gains TLS connect (host, port), send, receive with timeout and close. The core stays free of
  TLS code.
- POSIX port: mbedTLS. Install with `brew install mbedtls` if missing. Verify certificates against
  `/etc/ssl/cert.pem`.
- ESP-IDF port: `esp-tls` with `esp_crt_bundle_attach`. Before connecting, check that
  `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` is at least 55000 bytes, the same floor as
  `lib/Trmnl/TrmnlClient.cpp:25`. Below it, log `derp: low heap` and fail.
- Certificate checks are always on. There is no insecure mode.

## 2 — DERP map in the cache  *(DECIDED)*

- Parse `DERPMap` from the MapResponse. Keep only regions that a cached peer uses as `HomeDERP`, plus our own home
  region: region ID, and the first node's hostname, IPv4 and DERP port (443 when unset). Cap 8 regions.
- Cache format version 2 adds the region records. A version 1 blob reads as "no cache", so the slow path rebuilds it.
- Our home region = the `HomeDERP` that most cached peers share. A tie goes to the lowest region ID. Send it as
  `Hostinfo.NetInfo.PreferredDERP` in every map request, as MicroLink does.

## 3 — DERP client  *(DECIDED)*

- **Files:** `src/core/derp.c`, with frame encode and decode separated from I/O so tests can run them.
- Connect: TLS to the region host, `GET /derp` with `Upgrade: DERP`, read ServerKey, send ClientInfo (NaCl box of a
  JSON with `version`), read ServerInfo.
- Send: `SendPacket` with the peer node key and the payload. Receive: `RecvPacket` gives the source key and the
  payload. Answer `Ping` with `Pong`. Ignore `KeepAlive` and unknown frames.
- Keep at most two DERP connections: our home region always, and the peer's home region when it differs. Peers
  answer through our home region.
- Disco Ping and Pong and WireGuard packets travel inside `SendPacket` / `RecvPacket` unchanged.

## 4 — Path choice  *(DECIDED)*

- The port reports a network ID: the IPv4 address of the Wi-Fi default gateway. The POSIX port reads the default
  route. The ESP-IDF port reads it from the STA netif.
- A small store blob `paths` remembers, per network ID and peer, which path last worked: direct endpoint or DERP.
  Cap 8 entries, least recent replaced.
- On the first packet for a peer: try the remembered path first. With nothing remembered, send disco Pings to the
  direct endpoints and wait 300 ms. With no Pong, connect DERP and send the Ping there. Use the first path that
  answers, then save it.
- A remembered path that fails falls back to the other path in the same wake.

## 5 — CrossInk  *(DECIDED)*

- Point the `tailesp32` submodule at the head of `feat/derp`. No app code changes unless the build needs them.
- `docs/tailnet.md`: remove "direct path only", add the DERP behaviour and the TLS heap floor.
- `CHANGELOG.md` → `## [Unreleased]` → `### Added` (or `### Changed` if 005's line is already released): one
  user-facing line about working away from home.

## Sequencing

1, 2, 3 with native tests, then the Mac checks, then 4, then CrossInk 5 and the hardware checks.

## Decisions — locked ✅

- Our home DERP region is the region most cached peers use. Rejected: latency probing (needs STUN and netcheck).
- Certificate checks are always on. Rejected: an insecure mode like `lib/Trmnl`'s `TODO(trmnl-https)`.
- At most two DERP connections. Rejected: one per peer region.
- The last working path is remembered per network. Rejected: always trying direct first (300 ms lost on every
  travelling wake).
- The ESP-IDF port fails DERP below 55000 bytes of contiguous internal heap. Rejected: TLS buffers in PSRAM.

## Out of scope for this handover

- NAT hole punching (CallMeMaybe, STUN). DERP covers travel. A direct path away from home is a later optimisation.
- Peer relays and Tailscale's UDP relay feature.
- Running our own DERP server.

## Acceptance / pre-merge checklist

- `tailesp32`: `ctest` passes. New tests: frame encode and decode round trips, ClientInfo box opens with the
  server key, cache version 1 is rejected, path-memory replacement.
- Mac: `tailesp32-cli ping 100.85.148.88 --force-derp` gets 3 echo replies. Add the `--force-derp` flag
  (skips direct endpoints). Print the timings and put them in README "Measurements".
- CrossInk builds: `pio run -e x4-pro`, `-e x4-pro-debug`, `-e default`.
- Hardware, X4 Pro on a phone hotspot (away from the home LAN): three timer wakes refresh the dashboard from the
  100.x Server URL. The log shows the DERP path. Report the time from `GOT_IP` to tunnel ready for all three. Each
  must be at most 10 000 ms, the slow-path budget in the log.
- Back on the home Wi-Fi: the next wake uses the direct path (log line), with no DERP connection.
- `git log --format=%B | grep -i -E "claude|anthropic|co-authored|generated with"` in `tailesp32` prints nothing.

---
**On completion:**
1. Update `.handovers/log_tailnet.md`. Set this handover's status to ✅ done and fill in the Completed date. If you
   could not finish, set 🔄 in-progress and add a note row that describes what remains.
2. Add each locked decision to the log's **Decisions so far**. Write one line: the gist plus the rejected
   alternative, linking this doc by name. The detail stays here.
3. Move anything this work made sharp out of **Not yet specified**. Move anything it ruled out into **Out of scope**.
4. Commit, push, and open both PRs: `tailesp32` `feat/derp` into its `main` (no handover number, no attribution),
   then CrossInk `feat/tailnet-derp` into `main` with the submodule at the head of `feat/derp`. Say in the CrossInk
   PR body that the submodule moves to the merged commit before merge. CrossInk PR title: "Handover 006 — DERP
   relay for the tailnet".
