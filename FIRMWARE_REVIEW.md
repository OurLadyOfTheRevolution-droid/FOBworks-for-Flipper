# FOBworks for Flipper — firmware review

I reviewed this FAP against the SGP Card Mini firmware, the public Security+ /
KeeLoq / Honda KR5 sources in CITATIONS_AND_REFERENCES.md, and the CC1101 /
HCS datasheets those files already cite. This note is my lab record: what was
wrong, what I changed, and what I left alone.

No new transmit attack paths, jam recipes, or key-recovery shortcuts. The radio
already has a bounded TX session. The work here is decode correctness, buffer
safety, and abortable TX.

— OurLadyOfTheRevolution-droid

## What this tree is

FOBworks for Flipper is a Sub-GHz FAP on official firmware 1.4.2 / 1.4.3
(API 87.1, target 7), version 1.4. Live receive, KeeLoq manufacturer-key check,
a library of `.sub` files, and a JSON dashboard link (off until Settings) are
the product. Menu names put the tool first (FOBscan, FOBclone, FOBcatch,
FOBback, FOBpwn, FOBreport, FOBfreq, FOBtrack, FOBroll).

The host FAP is slim EXTERNAL. Vehicle tables live in `fw_catalog.fal`.
Sec+ 1.0/2.0, Scher-Khan, Hitag2, Mazda, Honda (incl. KR5), and Toyota live
in `fw_force.fal`. Both plugins are `fal_embedded` under `/assets/plugins/`,
mapped on demand and unmapped on the main menu. Host loader sizes after the
lab-scene wiring: `.text` 60648 / 61352, `.rodata` 15349 / 17645, `.bss`
5189 / 5924. Anything that grows the host image still has to earn that space.

Host tests live in `source/fobworks_flipper/tools`. `make test` is the check
I run after these edits.

## Reference firmware

I compared this tree side by side with FOBworks for SGP (ESP32-S3 + CC1101,
v4.01):

- **TX policy.** Both trees cap duration, frame count, and duty cycle. The
  Flipper session in `flipper_tx_session.c` already rejects >80% duty and >8
  frames, and cancel is idempotent across the 32-bit tick wrap. SGP is stricter
  on the control surface: every serial / WebSocket / HTTP command needs a
  per-device access code stored in NVS.
- **Short chips.** SGP keeps a Renault ~66 µs PWM family in its tests. rtl_433
  documents Honda KR5 Manchester at ~60 / 120 µs. This FAP used to drop every
  edge shorter than 75 µs in the RX ISR and in the TE histogram, so those
  frames never reached an Auto-safe decoder.
- **Vault reload.** SGP host tests treat a keys file as a full replace. This
  vault’s upsert took the first unused slot before scanning the rest of the
  table, so a hole in the middle duplicated a later name on replace.

Public protocol sources (not the SGP tree):

- Clayton Smith, argilo/secplus: Security+ 1.0 `encode` / `decode` /
  `encode_ook` (2000 baud, symbols 0001 / 0011 / 0111) and Security+ 2.0
  Manchester with `_ORDER` / `_INVERT` scramble.
- Flipper Zero `lib/subghz/protocols/secplus_v1.c` and `secplus_v2.c`.
- rtl_433 `src/devices/secplus_v1.c`.
- Microchip AN1064 / HCS301: KeeLoq 528-round NLFSR (this tree already
  matches the published vectors).
- TI CC1101 SWRS061 Table 25: PATable 0xC0…0x03. Live TX/RX uses official
  SubGhz device presets, not `flipper_cc1101_presets.c`.

## Bugs I fixed

### 1. Security+ 1.0 decoded the wrong format

`flipper_decode_secplus1` implemented a 40-bit binary PWM word plus a 4-bit
popcount. The air format is two packets of 21 trits, no checksum. Forced
decode of a spec-correct frame failed; the old synthetic “hits” were checksum
accidents.

I replaced it with the public OOK grouping (4 chips / symbol) plus a LOW-width
pair fallback, recovering rolling (bit-reversed 32-bit) and fixed (base-3).
Auto stays off: there is still no transmitted checksum. Forced synthetic
round-trip is 100% on `make test`.

### 2. KeeLoq vault upsert / keys.txt parse

Upsert broke out of the scan on the first unused slot, so replacing a name
that sat after a hole created a second entry. `from_text` compared with a
line that is not NUL-terminated at `nlen`, so in-place match never fired.

Upsert now searches for the name across the whole table, then takes a hole.
`from_text` clears the vault, parses names exactly, and reports unique count.
`tools/test_keyvault.c` covers the hole-replace and reload cases.

### 3. RX floor ate Honda KR5 / 66 µs chips

`flipper_capture_rx_cb` and `flipper_estimate_te` discarded durations under
75 µs. Honda KR5 is Auto-safe and specified at ~60 µs marks.
`FLIPPER_MIN_PULSE_US` is now 40 in both paths.

### 4. Sequence TX held a caller pointer

`flipper_capture_tx_sequence` stored `tx_caps = caps` after the header said
TX never keeps a caller pulse pointer. The sequence path now copies up to
eight `FlipperPulseBuf`s on the heap and frees them when the session goes
idle, when worker start fails, and in `flipper_capture_free`.

### 5. Classifier pulse writer could overflow

`test_classify.c` `push()` allowed `len < 1024` into a 256-slot buffer. It
now stops at `FLIPPER_PULSE_MAX`.

### 6. Security+ 2.0 was a low-bit trit fold

I replaced it with argilo/Flipper Manchester + `_ORDER`/`_INVERT` scramble.
Force-only. Segment decode splits on raw LOW blanks so odd-length gaps do not
break pairing. Device build packs the scramble tables.

### 7. CC1101 preset table footgun

`flipper_cc1101_presets.c` quarantined (fail-closed stubs). Still not in
`application.fam`. Live radio uses official SubGhz presets only.

### 8. External radio “connected” lied

`radio_loader_is_connected()` no longer treats OTG power as proof. After OTG
is up it asks `subghz_devices_is_connect` on `cc1101_ext` (SPI VERSION under
the hood). Version helper accepts CC1101 VERSION `0x04` / `0x14` (SGP).
Advanced Settings can pick either radio; a failed external probe falls back
to the built-in one.

### 9. Land Rover predict window overstated

Truncated 256-edge captures cannot support a 256-step counter window.
`predict_window = 0` with a trunc-risk note.

### 10. Host FAP ran out of loader headroom

Security+2.0 alone pushed `.text` over the API 87.1 cap. I split the app:
slim EXTERNAL host, `fw_catalog.fal` for the full year table, `fw_force.fal`
for force extras. Both plugins are embedded in the FAP, mapped on demand, and
dropped on the main menu. FOBclone / FOBcatch / FOBback use catalog accessors.

### 11. FOBback Make list was blank

Entering FOBback malloc'd the guided union with five embedded capture
results (~11 KB) before `fw_catalog.fal` could map. On a tight heap the
catalog load failed, `flipper_fbk_make_count()` returned 0, and the Make
submenu showed only the header. Caps are a separate heap block allocated
on the listen scene now; Make maps the catalog first.

### 12. Force extras and link auth

Security+ 1.0 / 2.0, Scher-Khan, Hitag2, Mazda, Honda (incl. KR5), and Toyota
parsers live in `fw_force.fal` (host stubs only). Auto stays clean for
force-only families; Mazda and Honda KR5 remain Auto-safe via the stubs.
Dashboard Link Auth installs a fresh random 6-digit code when turned On —
never a guessable default. Commands honor `auth` / `code=` / `new_code=`.
The WiFi bridge requires `AUTH:<BRIDGE_KEY>` before forwarding. External
CC1101 uses `subghz_devices_is_connect` on `cc1101_ext` after OTG.

Live Security+ `.sub` captures from owned hardware go in
`source/deliverables/secplus-live-captures/`; `make secplus-live` reports
them for CORPUS_HONESTY.md. None are checked in yet.

## Measurements

`cd source/fobworks_flipper/tools && make test`

- Link, library contract, TX session, rolling-pwn analyzer, saved-check,
  keyvault, Hitag2, link-auth, new modules: pass.
- Vehicle decoder: Honda force-only still holds; Security+1.0 ternary and
  Security+2.0 Manchester+scramble vectors recover fields and stay out of
  Auto (Security+2.0 may still be claimed by KeeLoq on Auto — registry policy
  keeps Security+2.0 force-only).
- Classify (`40 --check`): Security+1.0 and Security+2.0 forced 100%. Overall
  forced 100% on this seed. Scher-Khan force path is covered in `test_sim`.
- `ufbt` APPCHK: Target 7, API 87.1. Host `.text` 60648 / 61352 (704 free)
  after parking Mazda/Honda/Toyota in `fw_force.fal` and wiring FOBfreq /
  FOBtrack / FOBroll.

## Remaining debt

**More OEM FALs.** Catalog and force extras are the first two plugins.
PSA Mode 0x23 is still host-side (`oem_wire`); peel it into `fw_force.fal`
when headroom or flash trade-offs say so. Same ABI (`FOBWORKS_PLUGIN_APPID` /
`FOBWORKS_PLUGIN_ABI`). I am not forking into firmware for size.

**Dashboard auth.** Gate is on. Enabling Link Auth mints a random 6-digit
code (shown in Advanced Settings). The WiFi bridge also requires
`AUTH:<BRIDGE_KEY>` before forwarding. Rotate over the link with
`{"cmd":"auth","code":"<current>","new_code":"...."}` when needed.

**Hitag2.** Cipher helpers sit in `fw_force.fal` (not Auto). No Sub-GHz
pulse decoder for LF Hitag2 in this FAP.

**Security+ Auto.** Waits on live capture false-positive data in
`secplus-live-captures/`. `make secplus-live` is ready; the folder is empty.

**Land Rover preamble vs 256 edges.** Unchanged physics; advisory is honest.

**Manufacturer-key masking.** Unchanged: masking is not encryption.

**FOBfreq fingerprint.** TE-proxy ppm vs 400 µs until SubGhz exposes CC1101
FREQEST through the device API.

## TX / RF notes (existing contract)

`flipper_tx_session_begin` already:

- refuses `duration_ms == 0` or `frame_count == 0`
- refuses duration > 30 s, frames > 8, duty > 80%
- keeps a generation id that skips 0 on wrap
- times out with signed tick subtract so 32-bit wrap still works

The worker stops async TX, returns to idle, then starts the next frame. That
is the CC1101 rule (no PA hot-switch). Scene `on_exit` calls
`flipper_capture_stop`, which cancels an active session, then
`tx_wait_stopped`. App exit uses `FlipperTxCancelAppExit`. Those limits stay.

Jam buffers in FOBcatch / the dashboard dispatcher are already built to the
80% cap (100 ms low vs 400 ms on). I left them alone.

## Round after the security review

Bugs fixed in this pass:

1. KeeLoq no longer advertises a numeric prediction window from ciphertext
   low bits when no manufacturer key recovers.
2. KeeLoq next-frame synthesis includes the 10-bit serial discriminator in
   the plaintext, matching HCS3xx / AN1064.
3. VAG AUT64 no longer claims a key_index from a vacuous `button <= 0x0F`
   check.
4. FOBcatch no longer jam-and-replays a Ford frame that no decoder accepted.
5. Remote scan now flushes and streams `signal` events to the dashboard.
6. Link Auth generates a random 6-digit code; the WiFi bridge requires
   `AUTH:<BRIDGE_KEY>` before forwarding.
7. Status / heartbeat report `furi_hal_power_get_pct()` instead of a
   hardcoded 100%.

New capabilities:

- `flipper_kl_clone_next` — eavesdrop-only next-code synthesis under a known
  manufacturer key (host-tested).
- FOBreport / FOBfreq / FOBtrack / FOBroll — read-only health grade, TE-proxy
  crystal fingerprint, TPMS↔RKE co-occurrence, generalized RollBack analyzer.
- Mazda / Honda / Toyota parsers parked in `fw_force.fal` (host stubs).

## Next steps

1. Drop owned-hardware Security+ 1.0 / 2.0 `.sub` files into
   `secplus-live-captures/` and paste `make secplus-live` into
   CORPUS_HONESTY.md before any Auto debate.
2. Wire real CC1101 FREQEST into FOBfreq when SubGhz exposes it.
3. Port further OEM families as FALs when they earn the flash.

KeeLoq decrypt in this tree already follows AN1064 (NLF 0x3A5C742E, 528
rounds) and passes the three published vectors. Clone synthesis now uses
the same plaintext layout.
