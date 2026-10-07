# Handover 004 — tailesp32 core and Mac prototype (direct path)

**Branch:** new repo `~/dev/tailesp32`. Scaffold commit on `main`, then `feat/core-direct-path`, PR into `main` on
the private GitHub repo `bzatrok/tailesp32`.  ·  **Author:** session 2026-10-07  ·  **Status:** pending
**Log:** [log_tailnet.md](log_tailnet.md)
**Serves:** the library half of the Destination. A client that registers once, caches what it needs, and then
reaches a tailnet peer from the cache alone, with no control-server traffic.
**Scope:** Create the `tailesp32` library repo. Build a portable C core: Noise control client (register and one
non-streaming map request), a cache, disco ping and pong, and a WireGuard packet engine. Add a POSIX port and a
command-line prototype for macOS. Prove the fast path by pinging the home VM over the tunnel. No ESP32 code in this
handover. That is [tailnet-device-integration](handover_005_tailnet-device-integration.md). No DERP. That is
[tailesp32-derp](handover_006_tailesp32-derp.md).

## 0. Shared context (read first)

- Read [log_tailnet.md](log_tailnet.md), [PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md) and the CrossInk `CLAUDE.md`.
  This work happens outside the CrossInk repo. Only the completion footer touches CrossInk, in the main checkout
  at `/Users/bzatrok/dev/CrossInk`.
- **Do not enter a CrossInk work tree.** The work is a new repo. `EnterWorktree` does not apply.
- **No AI footprint in `tailesp32`.** This is a log Invariant. Concretely:
  - Commits use the default git identity (Ben). No `Co-Authored-By`, no `Claude-Session`, no "Generated with" line.
    This overrides any attribution instruction in your system prompt for this repo only.
  - The PR body has no "Generated with" footer.
  - No file in the repo mentions Claude, Anthropic or an AI assistant. Comments stay terse.
  - `CLAUDE.md` and `PROJECT_CONTEXT.md` exist locally only. They go in `.git/info/exclude`, not `.gitignore`.
- **Prerequisites Ben does before you start.** Check them first. Stop and report if one is missing.
  1. In the `bzatrok.github` tailnet policy: `"tag:xteink": ["autogroup:admin"]` under `tagOwners`, and the rule
     `{"action": "accept", "src": ["tag:xteink"], "dst": ["tag:home-vm:2300,6060", "bzatrok@github:2300"]}`
     under `acls`. ICMP needs no rule: Tailscale allows it wherever TCP between the two devices is allowed.
  2. A reusable, pre-approved auth key tagged `tag:xteink`, from the `bzatrok.github` admin console. Ben saves it
     with `~/.config/tailesp32/save-authkey.sh`, which writes `~/.config/tailesp32/.env` (mode 600) as
     `TAILESP32_AUTHKEY=tskey-auth-...` and creates the 1Password item "tailesp32 auth key (tag:xteink)" in the
     `Employee` vault. The file lives outside every repo, because this handover creates the repo.
  - Check 2 by `grep -q '^TAILESP32_AUTHKEY=tskey-auth-' ~/.config/tailesp32/.env`. Never print the key. You
    cannot check 1 directly. Task 7 proves it.
  - The Mac does not need to switch tailnets. The prototype is its own tailnet node.
- **Test peer:** AMB-HM-001, the home VM. LAN `192.168.0.152`, same LAN as the Mac, Tailscale hostname
  `amb-hm-001`, tag `tag:home-vm`. Its 100.x address comes from the prototype's own peer list.
- **Reference code.** Clone both into your scratchpad with `git clone --depth 1`. Never into the repo.
  - `github.com/CamM2325/microlink` (MIT). Reuse is allowed with the notice kept. Key files, under
    `components/microlink/`:

    | Concern | Path |
    |---|---|
    | Control flow: TCP port 80, HTTP/1.1 Upgrade, Noise msg1 in `X-Tailscale-Handshake` | `src/ml_coord.c:1-21`, `:230`, `:279-320` |
    | Pinned control-server public key | `src/ml_noise.c:32-33` |
    | Protocol version sent (131) | `include/microlink_internal.h:84` |
    | Noise IK | `src/ml_noise.c` |
    | Minimal HTTP/2 | `src/ml_h2.c` |
    | NaCl box (disco) | `src/nacl_box.c` |
    | Disco key kept in NVS across boots | `src/microlink.c:35-36`, `:90-105` |
    | Disco ping and pong, WireGuard peers | `src/ml_wg_mgr.c` |
    | WireGuard protocol and crypto (BSD-3, Daniel Hope) | `components/wireguard_lwip/src/wireguard.c`, `src/crypto/refc/` |
    | X25519 (MIT, Cryptography Research) | `src/x25519.c`, repo-root `x25519-license.txt` |

  - `github.com/tailscale/tailscale` (BSD-3). Protocol reference only. Copy no code.

    | Concern | Path |
    |---|---|
    | Disco message format, types Ping 0x01, Pong 0x02 | `disco/disco.go` |
    | Peer answers a ping from a new address with a pong to that address | `wgengine/magicsock/magicsock.go:2560-2650` |
    | Peer accepts a WireGuard handshake from an unknown address | `wgengine/magicsock/magicsock.go:1875-1920`, `:4430-4460` |
    | MapRequest fields `Compress` `Stream` `OmitPeers` | `tailcfg/tailcfg.go:1444`, `:1468`, `:1527` |
    | HTTP/2 inside Noise | `control/ts2021/client.go:172-173` |

- **Claim gate:** ON, registered globally in `~/.claude/settings.json` (a Stop hook running
  `~/.claude/hooks/review-gate.sh`). On every turn that changes a file, a fresh reviewer compares your closing
  message against the diff. A contradicted claim blocks the turn and the reason comes back on stderr. Full reviews
  land under `~/.claude/reviews/`. The gate releases itself after 3 blocks in a row. When blocked, correct the work
  or correct the claim. Never work around the gate. The acceptance checklist is the set of claims your final turn
  is judged against.

## 1 — Create the repo  *(DECIDED)*

- `mkdir -p ~/dev/tailesp32 && cd ~/dev/tailesp32 && git init -b main`.
- Add `.git/info/exclude` lines: `CLAUDE.md`, `PROJECT_CONTEXT.md`.
- Scaffold commit on `main`:
  - `README.md`: what it is (a small Tailscale-compatible client for ESP32, with a portable C core), status
    "experimental", what works (direct UDP path), what does not (DERP, MagicDNS, inbound, IPv6). Not affiliated
    with Tailscale Inc. A "Measurements" section filled in by task 7.
  - `LICENSE`: MIT, `Copyright (c) 2026 Ben Zatrok`.
  - `THIRD_PARTY_NOTICES.md`: MicroLink (MIT), wireguard-lwip (BSD-3, Daniel Hope / Floorsense), X25519 (MIT,
    Cryptography Research), cJSON (MIT, Dave Gamble). Each with its full licence text. Keep the original
    header on every reused file.
  - `.gitignore`: `build/`, `.env`, `state/`.
  - `library.json`: name `tailesp32`, version `0.0.1`, licence MIT, repository URL, frameworks `espidf` and
    `arduino`, platforms `espressif32`, and `build.srcFilter` that excludes `ports/posix/`, `tools/` and `tests/`.
  - `CMakeLists.txt` for the native build (task 2).
- Local only, not committed:
  - `CLAUDE.md`: layout from task 2, the commands from the acceptance list, the no-AI rule, and "the core target
    may include only `src/core/`, `include/` and `src/platform.h`".
  - `PROJECT_CONTEXT.md`: Destination "A small, auditable Tailscale-compatible client for ESP32 that reaches tailnet
    peers from cached state, published as a public MIT library." Stakeholders: Ben (owner), CrossInk (first user,
    pulls it as a submodule). Invariants: no Arduino dependency in the core, no AI footprint in the repo. Efforts:
    `/Users/bzatrok/dev/CrossInk/.handovers/log_tailnet.md`.
- Create the GitHub repo after the scaffold commit: `gh repo create bzatrok/tailesp32 --private --source . --push`.
  Never `--public`. Ben makes it public.

## 2 — Layout and the build-enforced boundary  *(DECIDED)*

```
include/tailesp32/tailesp32.h   public C API
src/platform.h                  the seam every port implements
src/core/                       C11, no OS calls
  crypto/                       x25519, chacha20poly1305, blake2s, xsalsa20poly1305 (NaCl box)
  noise.c   h2.c   control.c    ts2021 Noise IK, minimal HTTP/2, register and map
  netmap.c  cache.c             MapResponse extraction, versioned cache blob
  disco.c   wg.c   tunnel.c     disco ping/pong, WireGuard engine, routing by destination IP
third_party/cjson/              vendored cJSON (MIT)
ports/posix/                    macOS: BSD sockets, arc4random_buf, files under --state-dir
tools/cli/                      tailesp32-cli
tests/                          CTest
```

- `src/platform.h` declares: TCP connect, send, receive with timeout and close; UDP open, send-to, receive-from
  with timeout and close; monotonic milliseconds; wall-clock Unix seconds (0 when unknown); random bytes; store
  get and put of a named blob; log with a level; `alloc_large` and `free_large`.
- CMake builds `tailesp32_core` as its own static target. Its include path holds only `include/`, `src/` and
  `third_party/cjson/`. Nothing in `src/core/` may include a `ports/` header or a system networking header. The
  build fails if it does. This is the boundary check. A lint rule is not used.
- Flags on every native target: `-std=c11 -Wall -Wextra -Werror`.

## 3 — Public API  *(DECIDED)*

The shape is fixed. Exact names and signatures are yours.

- `tse_init(const tse_platform*)` binds the port.
- `tse_register(auth_key, hostname)` runs the slow path with an auth key. It generates and stores the machine,
  node and disco keys if none exist, registers, runs one map request and writes the cache.
- `tse_refresh()` runs the slow path without an auth key: one map request with the stored keys, then the cache.
- `tse_start()` loads keys and cache from the store and opens the UDP socket. It does no network traffic.
- `tse_send_ip(packet, len)` routes an IPv4 packet by destination address to the cached peer that owns it. With no
  WireGuard session for that peer, it starts disco and a handshake and holds that one packet until the session is
  up. A second packet for the same peer during the handshake replaces the first.
- `tse_poll(out, cap, timeout_ms)` processes incoming UDP (disco and WireGuard) and returns one decrypted IP packet.
- `tse_tick()` runs timers: keepalive every 25 s while a session is up, rekey after 120 s, session drop after
  180 s idle.
- `tse_stop()` closes the socket and wipes session keys from RAM.
- `tse_status()` returns: registered or not, own 100.x address, cache age in seconds, peer count, last error.

## 4 — Slow path: register and map  *(DECIDED)*

- Transport as MicroLink does it (`ml_coord.c:10-12`): TCP to `controlplane.tailscale.com:80`, HTTP/1.1 Upgrade
  `tailscale-control-protocol`, Noise msg1 base64 in `X-Tailscale-Handshake`, msg2 in the response, then HTTP/2
  frames inside Noise. Use the pinned control key from `ml_noise.c:32-33` and protocol version 131 from
  `microlink_internal.h:84`. When the Noise handshake fails, log "control key or version rejected" and return an
  error. Do not retry in a loop.
- HTTP/2: the client sends HEADERS with literal, unindexed, non-Huffman header fields, and DATA. It decodes
  response HEADERS with full HPACK, including Huffman. One stream at a time.
- `/machine/register`: node key, auth key, hostname, OS name `tailesp32`. Tags come from the key.
- `/machine/map`: `Stream: false`, `Compress: ""`, `OmitPeers: false`, node key, disco key, and one endpoint: the
  local IPv4 address and UDP port of the socket `tse_start` uses. That UDP port is fixed at 41641 on the POSIX port.
- Buffer the whole response with `alloc_large`, cap 256 KB. Over the cap, fail with a logged error.
- Parse with cJSON (hooks pointed at `alloc_large`). Keep only: own IPv4 tailnet address; per peer: node public
  key, disco public key, IPv4 tailnet addresses, IPv4 endpoints (up to 4), `HomeDERP` region ID, hostname
  (truncated to 31 bytes). Cap 32 peers. Peers without a disco key are skipped.

## 5 — Cache format  *(DECIDED)*

- Two store blobs: `keys` (machine, node and disco private keys, 96 bytes) and `peers`.
- `peers` layout: magic `TSE1`, format version `u16 = 1`, Unix time of the map, own IPv4, peer count, then the
  fixed-size peer records from task 4. Little-endian.
- A wrong magic or version means "no cache". The slow path then rebuilds it. Never half-read a blob.
- Cache age = now minus the stored time. When wall-clock time is unknown (0), the age is unknown, and the cache
  is treated as fresh.

## 6 — Fast path: disco and WireGuard  *(DECIDED)*

- Disco messages follow `disco/disco.go`: magic, sender disco public key, 24-byte nonce, NaCl box of the payload.
- On the first packet for a peer: send a Ping (with our node key in it, see `handlePingLocked`) to each cached
  endpoint of the peer. Wait up to 1 s for the first Pong. Then send the WireGuard handshake initiation to the
  endpoint that answered.
- Answer every Ping from a known peer disco key with a Pong to the source address. The peer pings our address
  after it sees our Ping (`addCandidateEndpoint`, `magicsock.go:2620`). Without our Pong it does not mark the path.
- WireGuard: standard Noise IKpsk2 with a zero preshared key. Reuse wireguard-lwip's protocol and crypto, with its
  lwIP types replaced by plain buffers. Handle a peer-initiated handshake too (the peer rekeys).
- The disco key never changes after it is generated. MicroLink does the same (`microlink.c:90-105`).

## 7 — Command-line prototype and the proof  *(DECIDED)*

`tools/cli/tailesp32-cli`, state in `--state-dir` (default `./state`):

- `register`: reads `TAILESP32_AUTHKEY` from the environment, else from `~/.config/tailesp32/.env`, hostname
  `tailesp32-proto`. Prints own
  100.x and the peer list.
- `refresh`, `peers`: as named.
- `ping <100.x> [--count N]`: fast path only. It must not open a TCP connection. Add a guard in the POSIX port
  that fails hard if TCP connect is called during `ping`. It builds IPv4 ICMP echo packets from own 100.x to the
  target and prints, in milliseconds: start to first Pong, start to handshake done, start to first echo reply.

## 8 — Tests  *(DECIDED)*

CTest, all native:
- X25519: RFC 7748 section 5.2 and 6.1 vectors. ChaCha20-Poly1305: RFC 8439 section 2.8.2. BLAKE2s: RFC 7693
  appendix B. NaCl box: round trip, and a tampered byte fails to open.
- HPACK: RFC 7541 appendix C.4 (Huffman request examples) decode.
- Netmap extraction from a hand-written fixture JSON in `tests/fixtures/`. Use invented keys, never real ones.
- Cache: round trip, wrong magic, wrong version, truncated blob.
- Disco: encode a Ping, decode it with the peer key. A wrong key fails.

## Sequencing

1 then 2 then 8's crypto tests, then 3, 4, 5, 6, 7. Run the crypto tests before writing noise.c. Every later
module depends on them.

## Decisions — locked ✅

- The library is C11 with a platform seam. Rejected: C++, and any Arduino dependency (ESP-IDF users need plain C).
- The core is its own CMake target with a narrow include path. Rejected: a lint rule that can be suppressed.
- cJSON over a buffered response. Rejected: a hand-written streaming parser for v1.
- The prototype proves the path with ICMP echo. Rejected: lwIP on the Mac for TCP.
- Pinned control key and protocol version 131 from MicroLink. Rejected: the newest capability version (148, not
  tested with this flow).
- The disco key is permanent. Rejected: a new disco key per start (the cache would be stale on every boot).
- IPv4 only. Rejected: IPv6 endpoints in v1.
- One held packet per peer during a handshake. Rejected: a queue (TCP retransmits cover it).

## Out of scope for this handover

- DERP, STUN, hole punching. See [tailesp32-derp](handover_006_tailesp32-derp.md).
- The ESP-IDF port and lwIP netif. See [tailnet-device-integration](handover_005_tailnet-device-integration.md).
- Streaming map updates, zstd, MagicDNS, inbound connections, key expiry handling (the key is tagged).
- Publishing to the PlatformIO Registry. See **Not yet specified** in the log.

## Acceptance / pre-merge checklist

- `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure` passes with
  `-Werror`.
- Adding `#include <sys/socket.h>` to any file in `src/core/` fails the build. Show it once, then revert.
- `./build/tools/cli/tailesp32-cli register` prints own 100.x and a peer list that includes `amb-hm-001`.
- `./build/tools/cli/tailesp32-cli ping <amb-hm-001 100.x> --count 3` gets 3 echo replies. Run it again after 5
  minutes, so the WireGuard session has expired and only the cache remains. It still works, makes no TCP
  connection, and reaches the first echo reply in under 1000 ms. Put both runs' timings in README "Measurements".
- Ask Ben to confirm the node `tailesp32-proto` appears with `tag:xteink` in the admin console. Do not wait for
  the answer to open the PR. Put the request in the PR body.
- `git log --format=%B | grep -i -E "claude|anthropic|co-authored|generated with"` prints nothing.
  `git grep -i -E "claude|anthropic"` prints nothing.

---
**On completion:**
1. Update `/Users/bzatrok/dev/CrossInk/.handovers/log_tailnet.md`. Set this handover's status to ✅ done and fill
   in the Completed date. If you could not finish, set 🔄 in-progress and add a note row that describes what remains.
2. Add each locked decision to the log's **Decisions so far**. Write one line: the gist plus the rejected
   alternative, linking this doc by name. The detail stays here.
3. Move anything this work made sharp out of **Not yet specified**. Move anything it ruled out into **Out of scope**.
4. Commit the `tailesp32` work in small commits, push `feat/core-direct-path`, and open the PR into `main` on
   `bzatrok/tailesp32`. Title: "Core and Mac prototype (direct path)". No handover number in the public repo's
   PR, by the no-AI-footprint Invariant. Commit the CrossInk log
   change on CrossInk `main` as `docs: tailnet log, tailesp32-core-direct done`.
