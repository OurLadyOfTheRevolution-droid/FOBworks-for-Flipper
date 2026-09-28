# CITATIONS & REFERENCES

This reference list records the papers, repositories, datasheets, and other
material consulted while developing FOBworks for Flipper. Project measurements
are kept separate from external references at the end.

---

## Academic Papers

1. **"Gone in 360 Seconds: Automotive Immobilizer Takeover"** — USENIX Security 2016
   - Covers the VW Group Crypto1 attack and analysis of AUT64/XTEA protocols.
   - Authors: Garcia, Verdult, van den Herik.

2. **"Dismantling Megamos Crypto: Wirelessly Lockpicking a Vehicle Immobilizer"** — IEEE S&P 2013
   - Examines vehicle-immobilizer cryptanalysis and side-channel techniques.

3. **"Dismantling the AUT64 Automotive Cipher"** — TCHES 2018
   - Presents a full cryptanalysis of AUT64, including its key structure and
     S-box/P-box analysis.

4. **"KeeLoq Side-Channel Analysis"** — CHES 2008
   - Describes differential power analysis of the KeeLoq NLFSR and CPA/DPA
     techniques.

5. **"Security Evaluation of the KeeLoq Rolling Code System"** — Various CHES proceedings
   - Discusses rolling-code analysis, synchronization windows, and counter
     prediction.

6. **"Hitag2 Correlation Attack"** — Multiple proceedings
   - Covers 48-bit key extraction by correlation, with 2^38 complexity.

7. **"RollBack: A New Time-Agnostic Replay Attack Against the Automotive
   Remote Keyless Entry Systems"** — USENIX/Black Hat USA 2022 (Csikor et al.)
   - Describes a replay-and-resynchronize attack and reports validated
     per-platform consecutive-code counts: Honda 5, Mazda-3 3, Nissan Latio
     2/5 s, Sylphy 2/8 s, and Teana immune. These figures inform the rollback
     analysis profiles.

8. **"Attacking Automotive RKE Security: How Smart are your 'Smart' Keys?"**
   — IACR ePrint 2024/1816
   - Identifies the modern Toyota key as a Microchip HCS362T KeeLoq encoder and
     surveys Honda (Hitag2) and Suzuki (12F635 KeeLoq) fob internals.

9. **Rolling-PWN (CVE-2022-27254)** — Kevin2600 and Wesley Li, 2022
   - Describes how Honda receivers advance the rolling-code window over a
     short run of consecutive counters, allowing a replay of that run to make
     an older captured command acceptable again.
   - Informs FOBpwn's region variants, wrap-aware consecutive-run check, and
     resynchronization replay. The same sequence check is used by the
     Subaru-style rollback profiles.

---

## GitHub Repositories

1. **Flipper-ARF** — Custom Flipper Zero firmware with enhanced SubGHz protocols.
   - Provides CC1101 register configurations, including VAG, PSA, KIA, Honda,
     Renault, and FCA presets.
   - Includes a VAG AUT64/XTEA decoder, Ford V0-V3 decoders, and Chrysler and
     GM decoders.
   - Implements the PSA protocol (Mode 0x23 XOR, Mode 0x36 TEA brute-force),
     a Hitag2 cipher core, and the Fiat V1 BCM attack.
   - Also includes region-unlock patterns.

2. **ProtoPirate** — SubGHz protocol analysis toolkit.
   - Includes the KIA/Hyundai V0-V7 protocol suite (CRC8/4, mixer, AES-128),
     a PSA brute-force engine and crypto modules, a timing database for 23
     protocols, and Hitag2 Fiat V1 and Renault V1 implementations.
   - Consulted alongside Flipper-ARF for the OOK-PWM/Manchester framing
     conventions used by pulse front ends that make the GM/Ford/Chrysler/
     KIA/VAG/PSA parsers available to live captures.
   - The Mazda V0 reference specifies Manchester at 250/500 µs, a 0xD7 sync,
     an inverted 64-bit payload, parity-selected byte mask, bit-interleaved
     counter, and additive checksum. It was reimplemented here as a calibrated,
     Auto-safe decoder.
   - The Fiat / Alfa Romeo V0 reference uses 64-bit Manchester fix|hop with no
     checksum; this implementation is force-only.
   - Fiat V1 uses 104-bit Manchester, header 0x0001, and an XOR checksum. Fiat
     V2/FCA uses 112-bit Manchester, header 0x0001, and an FCA hop when the
     byte6 high nibble is 0xD0. These paths were cross-checked against in-house
     `flipper_fiat_honda` decode logic and marked Auto-safe in FOBworks.
   - The Scher-Khan / Magicar alarm reference uses a long-HIGH header and start
     bit, symmetric short=0/long=1 data cells, a long-HIGH stop bit, and a
     51-bit "MAGIC CODE, Dynamic" serial/button/counter split. This implementation
     is force-only; the protocol has no transmitted checksum.
   - The Toyota / Lexus Denso reference uses 40-bit PWM, preamble-derived TE,
     2T/1T HIGH-bit encoding, and a transition-count entropy gate. This is a
     force-only structural read: the `[serial][button][counter]` split is
     applied to the enciphered payload, which has no transmitted checksum, so
     it is not part of the Auto chain.
   - The provisional Nissan RKE layout is based on this background; its field
     offsets remain best-effort estimates pending calibration.
   - The Suzuki reference specifies 64-bit PWM, a short/short preamble,
     HIGH-width bit encoding at 250/500 µs, `[counter 20][serial 28][button 4][CRC-8]`,
     and CRC-8 poly 0x7F over the payload. It was reimplemented as a calibrated,
     Auto-safe decoder.
   - The Land Rover / Jaguar V0 reference specifies 81-bit differential
     Manchester at 250/500 µs, a long short/short preamble, long-HIGH/long-LOW
     sync, a short-HIGH boundary, `[signature 24][serial 24][counter 9][check 3]`,
     a 16-bit tail, trailing '1', count-parity check, and `0xFFFF`/`0x7FFF`
     tail selection. The implementation is Auto-safe but gated on the tail and
     check (no key needed). The transmitter's ~319-pair preamble exceeds a
     256-edge capture, so it anchors on ≥8 preamble pairs and relies on the
     gate. Validate against a real Land Rover/Jaguar capture before relying on
     the counter for rollback.
   - The GM 14-byte PWM reference (ABO1502T-class, rtl_433 `gm_car_remote`
     layout) informed the parser's integrity checks: button in the low nibble
     of b2, an additive-to-zero nibble checksum in its high nibble, and
     `byte_sum(b1..b13) & 0xFF == 0` over the frame. No 14-byte-PWM GM capture
     is available; the library's GM captures are KeeLoq/HCS.
   - Additional checksum/CRC-gated PWM framings were added as second paths to
     existing make decoders:
     - Mazda V1 Siemens-VDO (72-bit PWM, 4-bit XOR checksum)
     - Mazda Infinity (Manchester, FF FF D7 sync + 0x5A trailer, b5/b6 interleave
       + parity-keyed whitening + one's-complement, additive checksum)
     - Hyundai Santa Fe / Solaris TRW (80-bit PWM, CRC-8 poly 0x31 / init 0xFF)
     - Kia V7 (64-bit Manchester, one's-complement wire, 0x4C header + CRC-8
       poly 0x7F / init 0x4C over 7 bytes, plaintext serial/counter/button).
       Validate its counter against a real V7 capture before relying on it for
       rollback.
     - VAG ID48 pre-2004 (64-bit PWM, inverted 8-bit byte-sum checksum)
     - Hyundai/Kia RIO early (64-bit fixed-code PWM, 16-bit inverted checksum)

3. **Flipper-Zero-SUB-Analyzer** — Signal analysis utilities.
   - Provides a Shannon entropy calculator, an NRZ/Manchester/PWM/PPM
     encoding classifier, an FSPL (Free Space Path Loss) calculator, TX
     timing analysis, and a symbol-rate estimator.

4. **Flipper-Zero-SubGHz-Signal-Generator** — Radio-device management.
   - Covers internal/external CC1101 selection, region-unlock implementation,
     and OTG power management for external radios.

5. **flipper-zero-carjacker** — RollJam and code-grabbing research.
   - Describes a RollJam implementation and code-grabbing techniques.

6. **RocketGods-SubGHz-Toolkit** — SubGHz utilities.
   - Includes signal-generation patterns and frequency-sweep techniques.

7. **HiennNek/non-flipper-rolling-code-support** — KeeLoq manufacturer-key
   database.
   - Lists 73 real-world KeeLoq manufacturer keys, including gate, garage, and
     barrier (EU) keys; automotive and alarm (RU/CIS) keys; and factory-default
     patterns.

8. **DarkFlippers/unleashed-firmware** — Custom Flipper firmware.
   - Consulted for build-integration patterns, the `SubGhzEnvironment`
     protocol registry, and HAL compatibility notes.

9. **subarufobrob (tomwimmenhove)** — Subaru RKE reverse-engineering.
   - Canonical Subaru 80-bit OOK Manchester frame: ~1013 µs half-symbol,
     `[0x55 sync][serial 24][cmd|cmd][counter 20|checksum 4]`, nibble-XOR
     checksum, sequential (rollback-able) counter, command map
     (1=Lock 2=Unlock 0xA=Panic 0xB=Trunk).
   - Cross-checked against the PortaPack **Mayhem** `ui_keyfob` Subaru encoder
     for field order and checksum. The project reimplements it as a calibrated,
     Auto-safe decoder for FOBback's rollback profiles.

10. **Pandora DXL (alarm firmware)** — automotive RKE framing reference.
   - Provides upstream field-layout and checksum/CRC background for several
     OOK-PWM RKE framings (Mazda Siemens-VDO, VAG ID48, Hyundai/Kia RIO, Santa
     Fe TRW). These references were cross-checked against the ProtoPirate
     implementations above and reimplemented here as checksum-gated decoders.

11. **rtl_433 (merbanan)** — ISM-band device decoder collection
   - Documents the Honda KR5V2X/KR5V1X keyfob frame: 2-FSK Manchester at
     ~60/120 µs, an `EC 0F 62` manufacturer preamble,
     `[idx][deviceID32][event][counter24][rolling32]` payload, and OpenSafety
     CRC-8 (poly 0x2F, init 0x00) over the payload. This was reimplemented here
     as a calibrated, Auto-safe decoder.
   - Also provides broader context on automotive remotes, including GM
     ABO1502T, Chrysler, Ford, Continental, Siemens, and HCS361/HCS362 KeeLoq
     framings.

---

## Datasheets and technical documentation

1. **TI CC1101 Datasheet (SWRS061)** — Single-Chip Low-Cost Low-Power Sub-1 GHz RF Transceiver.
   - Documents register configurations, PATable values, and AGC settings.
   - Table 25 lists the PATable power levels.

2. **Microchip AN1064** — KeeLoq Code Hopping Decoder.
   - Describes the KeeLoq algorithm and NLFSR structure, including key
     derivation functions and learning modes.

3. **Microchip HCS301 Datasheet** — KeeLoq Hopping Code Encoder.
   - Documents frame structure, preamble timing, and the sync counter.

3a. **Microchip HCS362 Datasheet (DS40189)** — KeeLoq Code Hopping Encoder
   - Describes a 69-bit stream (32-bit hopping + 37-bit fixed: 28/32-bit serial, function,
     status, 2-bit CRC, 2-bit queue); TE 100/200/400/800 µs; used to scope the
     modern-Toyota (HCS362) framing variant.

4. **ISO/IEC 14443/15693/18092** — RFID/NFC standards.
   - Includes the referenced Hitag2 protocol specifications.

5. **SAE J2534** — Vehicle diagnostic communication standard.

6. **NXP PCF7936/PCF7952 Datasheets** — Transponder ICs.
   - Provide proprietary cipher specifications.

---

## Conference talks and community research

1. **DEF CON** — Automotive-security presentations.
   - Includes RKE attack and live RollJam demonstrations.

2. **Chaos Communication Congress (CCC)** — Car-security research.
   - Covers VW/Audi immobilizer attacks and KeeLoq key extraction.

3. **Black Hat** — Automotive-hacking sessions.
   - Covers challenge-response protocol analysis and side-channel techniques.

4. **Hardwear.io** — Hardware-security workshops.
   - Includes fault injection on RKE MCUs and power analysis of embedded
     cryptography.

5. **RTL-SDR Community** — Software-defined-radio research.
   - Covers signal-capture techniques and frequency-analysis methods.

6. **GreatScottGadgets Community** — RF-hardware research.
   - Includes CC1101 optimization and custom-preset development.

---

## Key databases

1. **KeeLoq Manufacturer Keys** — 73 entries drawn from:
   - HiennNek/non-flipper-rolling-code-support
   - Unleashed firmware key tables
   - Mayhem firmware databases
   - ARF firmware key collections
   - Field research and leaked databases

2. **Hitag2 Known Keys** — 16 entries, including:
   - Factory defaults
   - ASCII patterns
   - Renault V1 brute-force targets
   - Fiat V1 BCM keys

3. **VAG AUT64 Keys** — 3 hard-coded keys:
   - VW-2, VW-3 fixed global master keys
   - TEA key schedule for Type 2

4. **Protocol Timing Database** — timing data for 23 protocols:
   - te_short, te_long, te_delta, min_count_bit
   - PT2262, EV1527, CAME, FAAC, Sec+, and more

---

## Standards and specifications

1. **NIST FIPS 197** — AES-128 specification.
2. **RFC 7748** — X25519 and X448 elliptic-curve key agreement, not TEA/XTEA. The upstream reference mislabeled this RFC; it is not a source for the TEA/XTEA routines.
3. **CC1101 Application Notes** — TI documentation.
4. **Flipper Zero SDK Documentation** — FAP development guidelines.
5. **Furi OS Documentation** — threading, mutex, and event-flag APIs.
6. **Flipper Zero firmware ELF loader** (official tree, 1.4.3,
   `lib/flipper_application/elf/elf_file.c`)
   - Each `SHF_ALLOC` section is allocated from a single free heap block, with
     extra margin added to its size. This loader check informs the current FAP's
     decision to defer radio setup, share decoder scratch space, and leave the
     dashboard and full clone catalog out of the linked image.

---

## In-house measurements (not an external source)

The measurements below come from this project, not from the papers or
repositories listed above.

1. **Corpus honesty report** — `source/fobworks_flipper/CORPUS_HONESTY.md`
   - `tools/sub_check --report` was run on the private 162-file `.sub` set on
     24 Sep 2026.
   - Auto decoded 11 (6.8%): Fiat-V2 (4), BMW-CAS3-PPM (2), KeeLoq-HCS300 (2),
     KIA/Hyundai (2), and Suzuki (1). Force-only decoded 144 (88.9%); 7 (4.3%)
     did not decode.
   - The report contains protocol names and counts, not filenames, serials,
     hopping codes, or keys.

2. **BMW CAS3/CAS4 PPM gate** — structural read in `protocol/flipper_oem_wire.c`
   - The structural gate accepts constant-mark PPM (~250 µs HI, ~500/1500 µs
     LO) only after a ≥10 ms sync pair and a ≥64-bit run. The payload is treated
     as opaque (AES). This path was calibrated against in-house X5 captures;
     it does not copy an external decoder.

3. **Verdict card and `.sub` tags** — `protocol/flipper_verdict.c`
   - The verdict records confidence (`Auto` / `Force` / `None`) and one next
     action in `FT_Confidence` and `FT_Action` on a stock-RAW `.sub`.
   - It also suggests a preset (`try OOK` / `try 2FSK`) based on edge ratios in
     the same capture.
