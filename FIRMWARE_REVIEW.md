# FOBworks for Flipper — firmware review

Reviewed this FAP against the SGP Card Mini firmware, the public Security+ /
KeeLoq / Honda KR5 sources in CITATIONS_AND_REFERENCES.md, and the CC1101 /
HCS datasheets those files already cite. This note is the lab record: what was
wrong, what changed, and what stayed put.

No new transmit attack paths, jam recipes, or key-recovery shortcuts. The radio
already has a bounded TX session. The work here is decode correctness, buffer
safety, and abortable TX.

— OurLadyOfTheRevolution-droid

## What this tree is

FOBworks for Flipper is a Sub-GHz FAP on official firmware 1.4.2 / 1.4.3
(API 87.1, target 7). Live receive, KeeLoq manufacturer-key check, a library
of `.sub` files, and a JSON dashboard link (off until Settings) are the
product. Menu names put the tool first (FOBscan, FOBclone, FOBcatch, FOBback,
FOBpwn). Loader limits on this image are tight: `.text` 60328 / 61352,
`.rodata` 17437 / 17645, `.bss` 5105 / 5924. Anything that grows the FAP has
to earn that space.

Host tests live in `source/fobworks_flipper/tools`. `make test` is the check
run after these edits.

## Reference firmware

The sibling public tree is FOBworks for SGP (ESP32-S3 + CC1101, v4.01). That
tree was compared side by side:

- **TX policy.** Both trees cap duration, frame count, and duty cycle. The
  Flipper session in `flipper_tx_session.c` already rejects >80% duty and >8
  frames, and cancel is idempotent across the 32-bit tick wrap. SGP is stricter
  on the control surface: every serial / WebSocket / HTTP command needs a
  per-device access code stored in NVS.
- **Short chips.** SGP keeps a Renault ~66 µs PWM family in its tests. rtl_433
  documents Honda KR5 Manchester at ~60 / 120 µs. This FAP dropped every edge
  shorter than 75 µs in the RX ISR *and* in the TE histogram, so those frames
  never reached an Auto-safe decoder.
- **Vault reload.** SGP host tests treat a keys file as a full replace. This
  vault’s upsert took the first unused slot before scanning the rest of the
  table, so a hole in the middle duplicated a later name on replace.

Public protocol sources (not the SGP tree):

- Clayton Smith, [argilo/secplus](https://github.com/argilo/secplus): Security+
  1.0 `encode` / `decode` / `encode_ook` (2000 baud, symbols 0001 / 0011 /
  0111, header 0 then header 2).
- Flipper Zero `lib/subghz/protocols/secplus_v1.c`: same trit alphabet,
  `te_short = 500`, packet length 21.
- rtl_433 `src/devices/secplus_v1.c`: two-half ternary decode, 84–130 bit
  rows.
- Microchip AN1064 / HCS301: KeeLoq 528-round NLFSR (this tree already
  matches the keeloq-go vectors).
- TI CC1101 SWRS061 Table 25: PATable 0xC0…0x03. The live FAP TX path uses
  the official SubGhz device presets, not `flipper_cc1101_presets.c`.

## Bugs fixed

### 1. Security+ 1.0 decoded the wrong format

`flipper_decode_secplus1` implemented a 40-bit binary PWM word plus a 4-bit
popcount. The air format is two packets of 21 trits, no checksum. Forced
decode of a spec-correct frame failed; the ~3% “hits” on the old synthetic
generator were checksum accidents.

Replaced with the public OOK grouping (4 chips / symbol) plus a LOW-width
pair fallback, recovering rolling (bit-reversed 32-bit) and fixed (base-3).
Auto stays off: there is still no transmitted checksum, so a live false-positive
rate is unmeasured. Forced synthetic round-trip is 100% on `make test`
(`test_vehicle_decoder` and `test_classify`).

### 2. KeeLoq vault upsert / keys.txt parse

`flipper_keyvault_upsert` broke out of the scan on the first unused slot, so
replacing a name that sat after a hole created a second entry. `from_text`
compared `strncmp(stored, line, nlen + 1)` against a line that is not
NUL-terminated at `nlen`, so in-place match never fired, and a reload merged
into leftover slots.

Upsert now searches for the name across the whole table, then takes a hole.
`from_text` clears the vault, parses names exactly, and reports unique count.
`tools/test_keyvault.c` covers the hole-replace and reload cases.

### 3. RX floor ate Honda KR5 / 66 µs chips

`flipper_capture_rx_cb` and `flipper_estimate_te` discarded durations under
75 µs. Honda KR5 is Auto-safe in the registry and specified at ~60 µs marks.
`FLIPPER_MIN_PULSE_US` is now 40 in both paths. That is still above typical
CC1101 async glitch widths; it is below the KR5 / Renault band.

### 4. Sequence TX held a caller pointer

`flipper_capture.h` already said TX never keeps a caller pulse pointer after
enqueue. `flipper_capture_tx_sequence` stored `tx_caps = caps` anyway.
FOBback waits before freeing, so the GUI path was mostly safe; a dashboard or
scene that returned early was not.

The sequence path now copies up to eight `FlipperPulseBuf`s on the heap and
frees them when the session goes idle, when worker start fails, and in
`flipper_capture_free`. Single-frame TX already copied into `tx_owned`.

### 5. Classifier pulse writer could overflow

`test_classify.c` `push()` allowed `len < 1024` into a 256-slot
`FlipperPulseBuf`. It now stops at `FLIPPER_PULSE_MAX`.

## Measurements

`cd source/fobworks_flipper/tools && make test`

- Link, library contract, TX session, rolling-pwn analyzer, saved-check,
  keyvault: pass.
- Vehicle decoder: Honda force-only still holds; Security+1.0 ternary and
  Security+2.0 Manchester+scramble vectors recover fields and stay out of
  Auto (Sec+2.0 may still be claimed by KeeLoq on Auto — registry policy
  keeps Sec+2.0 force-only).
- Classify (`40 --check`): Security+1.0 and Security+2.0 forced 100%. Overall
  forced 100% on this seed.

## Bugs fixed (this pass)

### 6. Security+ 2.0 was a low-bit trit fold

Replaced with argilo/Flipper Manchester + `_ORDER`/`_INVERT` scramble. Force-
only. Segment decode splits on raw LOW blanks so odd-length gaps do not break
pairing. Device build packs the scramble tables. Scher-Khan is back on
device through `fw_force.fal` (force-only, same as host tests).

### 7. CC1101 preset table footgun

`flipper_cc1101_presets.c` quarantined (fail-closed stubs). Still not in
`application.fam`. Live radio uses official SubGhz presets only.

### 8. External radio “connected” lied

`radio_loader_is_connected()` no longer treats OTG power as proof. Version
helper accepts CC1101 VERSION `0x04` / `0x14` (SGP). External SPI probe still
not wired; OTG-enabled “connected” stays false until it is.

### 9. Land Rover predict window overstated

Truncated 256-edge captures cannot support a 256-step counter window.
`predict_window = 0` with a trunc-risk note.

### 10. Host FAP ran out of loader headroom

Sec+2.0 alone pushed `.text` over the API 87.1 cap. The host is now a slim
EXTERNAL FAP. Vehicle/year tables live in `fw_catalog.fal`; Scher-Khan lives
in `fw_force.fal`. Both are `fal_embedded` assets under `/assets/plugins/`,
mapped on demand and unmapped on the main menu. FOBclone/FOBcatch/FOBback
read the catalog through accessors, so the full year table is back on device
without sitting in host `.rodata` during Auto RX.

## Remaining firmware debt

**More OEM FALs.** Catalog and force extras are the first two plugins.
Further families (Hitag2, auth, heavier scenes) follow the same ABI
(`FOBWORKS_PLUGIN_APPID` / `FOBWORKS_PLUGIN_ABI`). Do not fork into firmware
for size.

**Dashboard auth.** Still no per-device access code on the JSON link. Lands
once host `.text` headroom after the plugin split is measured and reserved.

**Hitag2.** Host-only until a force-plugin slot has room (≥1 KiB `.text` in
that FAL is easy; keep it out of the host image).

**Sec+1.0 / Sec+2.0 Auto.** No transmitted Sec+1.0 checksum; Sec+2.0 scramble
is the integrity gate but Auto waits on live capture false-positive data.

**Land Rover preamble vs 256 edges.** Unchanged physics; advisory is honest.

**Manufacturer-key masking.** Unchanged: masking is not encryption.

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
80% cap (100 ms low vs 400 ms on). Leave them.

## Next steps

1. Measure host `.text` headroom after this plugin split; park Hitag2 in
   `fw_force.fal` only if that FAL stays small and Auto stays clean.
2. Collect real Security+ 1.0 / 2.0 `.sub` files (own hardware) for
   CORPUS_HONESTY.md before considering Auto.
3. Port SGP’s command-auth pattern once host headroom is reserved for it.
4. Wire external CC1101 SPI VERSION probe into `radio_loader_is_connected`.

KeeLoq decrypt in this tree already follows AN1064 (NLF 0x3A5C742E, 528
rounds) and passes the three published vectors. That core was left alone.
