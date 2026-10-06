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
- Vehicle decoder: Honda force-only still holds; new Security+1.0 vector
  recovers rolling/fixed and stays out of Auto.
- Classify (`40 --check`): Security+1.0 forced 100% / Auto 0% (registry
  policy). Overall forced 97.5% — Security+2.0 is still the weak row at
  66.7% on this seed. That 2.0 parser still takes only the low bit of each
  trit; it is not argilo’s 80-bit Manchester + scramble. Left alone in this
  pass (FAP size, and it is already force-only).

## Remaining firmware debt

**Security+ 2.0.** Public decode is 80- or 128-bit Manchester with the
`_ORDER` / `_INVERT` scramble (argilo `decode_v2`). This tree’s 62-symbol
low-bit fold will keep missing real openers. Same honesty rule as 1.0: no
Auto until a checksum or scramble check exists.

**Dashboard auth.** The Wi-Fi bridge comment is accurate: anyone on
`FOBworks-Flipper` can send JSON radio commands. SGP prints a 26-character
access code at boot and returns `unauthorized` without it. No token landed
in this FAP — `.text` has about a kilobyte of headroom, and a weak shared
password in the binary is worse than an honest “off until Settings” plus AP
password. Next step, if the size budget allows: generate a code on first run,
store it on SD, require it on `hello`.

**Unused CC1101 preset table.** `flipper_cc1101_presets.c` is not in
`application.fam`. Comments map `{0x04}` to MDMCFG4 and `{0x29}` to PATABLE;
on the CC1101 those addresses are SYNC1 and FSTEST. PATABLE is burst 0x3E.
That table must not load onto a live chip as-is. Live TX uses
`subghz_devices_load_preset()` with the official Ook650 / Ook270 / 2FSK
presets.

**Land Rover preamble vs 256 edges.** The V0 reference needs ~319 pairs.
`FLIPPER_PULSE_MAX` is 256 because the pulse buffer is copied through the
app allocation. The decoder already anchors on ≥8 preamble pairs. Counter
values from a truncated live capture stay untrusted.

**Hitag2.** Cipher core is in the tree and omitted from the FAP (size). Host
tests can see it; the device image cannot.

**External radio.** `radio_loader_is_connected()` returns true whenever OTG
was enabled. That is a stub, not a GDO0 / version-register check (SGP
accepts CC1101 version `0x04` and `0x14`).

**Manufacturer-key masking.** FOBLoq shows the stored mask; `mask_mfrkeys.py`
reverses it. Masking is not encryption. Treat the table as public material
that happens not to be plaintext in the listing.

**FAP size.** The Security+1.0 rewrite and vault/TX copies stayed small on
purpose. Security+2.0, Hitag2, or dashboard auth should not land in the same
change without a `ufbt` size readout.

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

1. Run `ufbt` on a 1.4.3 SDK and record `.text` / `.rodata` / `.bss` against
   the loader caps before any larger decoder.
2. Collect a handful of real Security+ 1.0 `.sub` files (own hardware) and
   add them to the private histogram in CORPUS_HONESTY.md. Only then consider
   Auto.
3. Port SGP’s command-auth pattern to the Flipper JSON link if the dashboard
   stays on a shared AP.
4. Rewrite Security+ 2.0 against argilo `encode_v2_manchester` the same way
   1.0 was rewritten — force-only until the scramble check is in.

KeeLoq decrypt in this tree already follows AN1064 (NLF 0x3A5C742E, 528
rounds) and passes the three published vectors. That core was left alone.
