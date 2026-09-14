---
title: Home Control
nav_order: 16
---

# Home Control (Xteink X4 Pro)

Home Control is a CrossInk-only feature that turns the X4 Pro into a remote for
Philips Hue lights and tado° heating. It is compiled only into the `x4-pro`,
`x4-pro-debug`, and `x4-pro-simulator` environments, gated by
`CROSSINK_APP_CAP_HOME_CONTROL`. Other devices carry no code for it.

Upstream CrossPoint's `SCOPE.md` excludes this kind of feature. It lives in this
fork by choice; expect to carry it through upstream rebases.

## Where it is

Home screen menu entry **Home Control**. Opening it performs the same minimal
network reboot the OPDS browser and KOReader sync use, then shows the Wi-Fi
picker (skipped when already connected) and a two-row hub: **Philips Hue** and
**tado°**. Back from the hub reboots to Home so the Wi-Fi heap is released.

## Philips Hue

Local only. Nothing leaves your LAN and no account is involved.

1. On first use the reader looks for a bridge over mDNS (`_hue._tcp`). If none
   answers within the query timeout, it asks for the bridge IP on the keyboard.
2. It then POSTs to the bridge every two seconds for up to 90 seconds until you
   press the round link button on the bridge. The bridge answers with an
   application key, which is stored on the SD card.
3. Rooms come from `GET /clip/v2/resource/room`; each room's `grouped_light`
   service is the control target. One `GET /clip/v2/resource/grouped_light`
   then fills every room's state, so the tile grid shows on/off and brightness
   at once (filled tile = on). Tapping a room opens on/off and brightness.
   Turn on/off, Brighter and Dimmer send one PUT each; the screen updates from
   what was sent, without a read-back round trip.
4. **All on** and **All off** use the `grouped_light` owned by the bridge's
   `bridge_home` resource, so they are one PUT that also reaches lights outside
   any room. If the bridge exposes no such group, the buttons loop the rooms.

The bridge uses a self-signed certificate, so TLS verification is disabled for
it, as it is for the other HTTPS clients in the firmware.

If the bridge later rejects the stored key (403), the key is cleared and pairing
starts again. **Change bridge** on the error screen re-enters the address.

## tado°

Cloud only. tado has no local API and no longer accepts password login; the
only supported route is the OAuth device-code flow.

1. First use: the reader asks tado for a device code and shows a QR code of the
   verification URL, the short address, and the user code. Scan the QR with
   your phone (the code is pre-filled), sign in, approve.
2. The reader polls the token endpoint at the interval tado specifies until
   approval, then stores the refresh token on the SD card.
3. Access tokens live about ten minutes and stay in RAM. Refresh tokens rotate
   on every use and are valid for up to 30 days or until used; every rotated
   token is written to the SD card immediately, because the previous one is
   dead as soon as the reply arrives.
4. Zones: `GET /me` resolves the home id (cached on SD), then the heating zones
   are shown as tiles with current and target temperature (one state request
   per zone). A filled tile marks a manual override. The zone screen offers
   +0.5°, −0.5° (manual override until the next schedule block, clamped to
   5–25 °C) and **Resume schedule**.
5. **All off** sends a power-off overlay until the next schedule block to every
   zone; **Resume all** deletes every overlay. tado has no home-wide endpoint,
   so both loop the zones and re-read each state afterwards.

If the API rejects a fresh token, the error screen says so; **Sign out** clears
the stored tokens and restarts the device-code flow.

## Storage

`/.crosspoint/home-control.json` holds `hueBridgeIp`, `hueAppKey_obf`,
`tadoRefreshToken_obf`, and `tadoHomeId`. Secrets are XOR-obfuscated with the
device MAC and base64-encoded, the same non-cryptographic scheme the KOReader
credentials use. Anyone with the SD card can recover them. Delete the file to
reset both services.

## Code layout

- `lib/HomeControl/HueProtocol.*`, `TadoProtocol.*` — pure request builders and
  response parsers over plain buffers. No Arduino includes; unit-tested natively.
- `lib/HomeControl/HomeControlHttp.*` — one HTTPS transport over the SDK's
  `SecureHttpClient` (firmware) or canned fixtures (`HomeControlFixtures.h`,
  simulator). Bodies stream into a caller-owned buffer (PSRAM on the X4 Pro).
- `lib/HomeControl/HueClient.*`, `TadoClient.*` — the two service clients.
  `TadoClient` owns token refresh and the rotate-then-persist rule.
- `lib/HomeControl/HomeControlStore.*` — the SD-card store.
- `src/activities/home_control/` — hub, Hue rooms, tado zones activities, and
  `HomeControlTileScreen.*`, the shared grid geometry and input for both.
- `src/components/TileGrid.*` — paged tile layout, hit testing and drawing;
  `TouchActionButtons::horizontal` — the bulk-action row.
- `src/components/icons/homeControlIcons.*` — tabler `home-bolt`, `bulb`,
  `temperature` icons.
- Launch: `NetworkBootTarget::HOME_CONTROL`, `ActivityManager::goToHomeControl()`,
  dispatch in `src/main.cpp`.

## Testing

- Native tests: `cmake -S test -B test/build && cmake --build test/build --target HomeControlProtocolTest && ctest --test-dir test/build -R "Hue|Tado"`.
- Simulator: the fixtures pair on the third attempt, approve the tado code on
  the third poll, and keep light and zone state so taps are visible. The
  bridge_home group is `gl-all`. See `docs/simulator.md` and
  `docs/development/fork-workflow.md` for the macOS build recipe and the
  scripted screenshot run.
- Hardware: watch the serial log for lines tagged `HC`. The hub prints free heap
  and largest block on entry; the TLS handshake is refused below roughly 35 KB
  free or 20 KB largest block.
