# CITATIONS & REFERENCES

This document lists all external sources that informed the development of FOBworks for Flipper.

---

## Academic Papers

1. **"Gone in 360 Seconds: Automotive Immobilizer Takeover"** — USENIX Security 2016
   - VW Group crypto1 attack, AUT64/XTEA protocol analysis
   - Authors: Garcia, Verdult, van den Herik

2. **"Dismantling Megamos Crypto: Wirelessly Lockpicking a Vehicle Immobilizer"** — IEEE S&P 2013
   - Vehicle immobilizer cryptanalysis, side-channel techniques

3. **"Dismantling the AUT64 Automotive Cipher"** — TCHES 2018
   - Full cryptanalysis of AUT64, key structure, S-box/P-box analysis

4. **"KeeLoq Side-Channel Analysis"** — CHES 2008
   - Differential power analysis on KeeLoq NLFSR, CPA/DPA techniques

5. **"Security Evaluation of the KeeLoq Rolling Code System"** — Various CHES proceedings
   - Rolling code analysis, synchronization windows, counter prediction

6. **"Hitag2 Correlation Attack"** — Multiple proceedings
   - 48-bit key extraction via correlation, 2^38 complexity

7. **"RollBack: A New Time-Agnostic Replay Attack Against the Automotive
   Remote Keyless Entry Systems"** — USENIX/Black Hat USA 2022 (Csikor et al.)
   - Replay-and-resynchronize attack; validated per-platform consecutive-code
     counts (Honda 5, Mazda-3 3, Nissan Latio 2/5 s, Sylphy 2/8 s; Teana
     immune) — used to parameterize the rollback analysis profiles.

8. **"Attacking Automotive RKE Security: How Smart are your 'Smart' Keys?"**
   — IACR ePrint 2024/1816
   - Identifies the modern Toyota key as a Microchip HCS362T KeeLoq encoder and
     surveys Honda (Hitag2), Suzuki (12F635 KeeLoq) fob internals.

9. **Rolling-PWN (CVE-2022-27254)** — Kevin2600 and Wesley Li, 2022
   - Honda receivers advance the rolling-code window on a short run of
     consecutive counters. A replay of that run makes an older captured
     command acceptable again.
   - Parameterizes FOBpwn: region variants, wrap-aware consecutive-run check,
     and the resync replay. The same sequence check is reused for Subaru-style
     rollback profiles.

---

## GitHub Repositories

1. **Flipper-ARF** — Custom Flipper Zero firmware with enhanced SubGHz protocols
   - CC1101 register configurations (VAG, PSA, KIA, Honda, Renault, FCA presets)
   - VAG AUT64/XTEA protocol decoder implementation
   - Ford V0-V3 protocol decoders
   - Chrysler protocol decoder
   - GM protocol decoder
   - PSA protocol (Mode 0x23 XOR, Mode 0x36 TEA brute-force)
   - Hitag2 cipher core and Fiat V1 BCM attack
   - Region unlock patterns

2. **ProtoPirate** — SubGHz protocol analysis toolkit
   - KIA/Hyundai V0-V7 protocol suite (CRC8/4, mixer, AES-128)
   - PSA brute-force engine and crypto modules
   - Protocol timing database (23 protocols)
   - Hitag2 Fiat V1 and Renault V1 implementations
   - Consulted (with Flipper-ARF) for the OOK-PWM/Manchester framing conventions
     used by the pulse front-ends that make the GM/Ford/Chrysler/KIA/VAG/PSA
     parsers reachable from a live capture.
   - Mazda V0 frame reference (Manchester 250/500 µs, 0xD7 sync, inverted 64-bit
     payload, parity-selected byte mask, bit-interleaved counter, additive
     checksum) — reimplemented in our own style as a calibrated, Auto-safe decoder.
   - Fiat / Alfa Romeo V0 frame reference (64-bit Manchester fix|hop, no checksum)
     — reimplemented as a force-only decoder.
   - Fiat V1 (104-bit Manchester, header 0x0001, XOR checksum) and Fiat V2/FCA
     (112-bit Manchester, header 0x0001, FCA hop when byte6 high nibble is 0xD0)
     — cross-checked against in-house `flipper_fiat_honda` decode logic
     and promoted to Auto-safe in FOBworks.
   - Scher-Khan / Magicar alarm PWM framing reference (long-HIGH header + start
     bit, symmetric short=0/long=1 data cells, long-HIGH stop bit; 51-bit
     "MAGIC CODE, Dynamic" serial/button/counter split) — reimplemented in our
     own style as a force-only decoder (no transmitted checksum).
   - Toyota / Lexus Denso 40-bit PWM framing reference (preamble-derived TE,
     2T/1T HIGH bit encoding, transition-count entropy gate) — reimplemented as a
     force-only, structural-only read: the `[serial][button][counter]` split is
     applied to the enciphered Denso payload, which carries no transmitted
     checksum, so it never joins the Auto chain.
   - Background for the provisional Nissan RKE layout (field offsets are our own
     best-effort guesses, pending calibration).
   - Suzuki frame reference (64-bit PWM, short/short preamble, HIGH-width bit
     encoding at 250/500 µs, `[counter 20][serial 28][button 4][CRC-8]`, CRC-8
     poly 0x7F over the payload) — reimplemented in our own style as a
     calibrated, Auto-safe decoder.
   - Land Rover / Jaguar V0 frame reference (81-bit differential Manchester at
     250/500 µs, long short/short preamble + long-HIGH/long-LOW sync + short-HIGH
     boundary, `[signature 24][serial 24][counter 9][check 3]` + 16-bit tail +
     trailing '1', count-parity check and `0xFFFF`/`0x7FFF` tail selection) —
     reimplemented in our own style as an Auto-safe decoder gated on the tail +
     check (no key needed). The transmitter's ~319-pair preamble overruns a
     256-edge capture, so we anchor on ≥8 preamble pairs and lean on the gate;
     validate against a real Land Rover/Jaguar capture before relying on the
     counter for rollback.
   - GM 14-byte PWM (ABO1502T-class, rtl_433 `gm_car_remote` layout) integrity
     reference — used to correct our GM parser to the documented checks: button
     in the low nibble of b2, an additive-to-zero nibble checksum in its high
     nibble, and `byte_sum(b1..b13) & 0xFF == 0` over the frame. (No 14-byte-PWM
     GM capture is on hand; the library's GM captures are KeeLoq/HCS.)
   - Additional checksum/CRC-gated PWM framings, reimplemented in our own style
     as second paths inside the existing make decoders:
     - Mazda V1 Siemens-VDO (72-bit PWM, 4-bit XOR checksum)
     - Mazda Infinity (Manchester, FF FF D7 sync + 0x5A trailer, b5/b6 interleave
       + parity-keyed whitening + one's-complement, additive checksum)
     - Hyundai Santa Fe / Solaris TRW (80-bit PWM, CRC-8 poly 0x31 / init 0xFF)
     - Kia V7 (64-bit Manchester, one's-complement wire, 0x4C header + CRC-8
       poly 0x7F / init 0x4C over 7 bytes, plaintext serial/counter/button —
       reimplemented in our own style; validate against a real V7 capture before
       relying on the counter for rollback)
     - VAG ID48 pre-2004 (64-bit PWM, inverted 8-bit byte-sum checksum)
     - Hyundai/Kia RIO early (64-bit fixed-code PWM, 16-bit inverted checksum)

3. **Flipper-Zero-SUB-Analyzer** — Signal analysis utilities
   - Shannon entropy calculator
   - Encoding classifier (NRZ/Manchester/PWM/PPM)
   - FSPL (Free Space Path Loss) calculator
   - TX timing analysis
   - Symbol rate estimator

4. **Flipper-Zero-SubGHz-Signal-Generator** — Radio device management
   - Internal/external CC1101 selection
   - Region unlock implementation
   - OTG power management for external radios

5. **flipper-zero-carjacker** — RollJam and code grabbing research
   - RollJam attack implementation
   - Code grabbing techniques

6. **RocketGods-SubGHz-Toolkit** — SubGHz utilities
   - Signal generation patterns
   - Frequency sweep techniques

7. **HiennNek/non-flipper-rolling-code-support** — KeeLoq manufacturer key database
   - 73 real-world KeeLoq manufacturer keys
   - Gate/garage/barrier (EU) keys
   - Automotive/alarm (RU/CIS) keys
   - Factory default patterns

8. **DarkFlippers/unleashed-firmware** — Custom Flipper firmware
   - Build integration patterns
   - SubGhzEnvironment protocol registry
   - HAL compatibility notes

9. **subarufobrob (tomwimmenhove)** — Subaru RKE reverse-engineering
   - Canonical Subaru 80-bit OOK Manchester frame: ~1013 µs half-symbol,
     `[0x55 sync][serial 24][cmd|cmd][counter 20|checksum 4]`, nibble-XOR
     checksum, sequential (rollback-able) counter, command map
     (1=Lock 2=Unlock 0xA=Panic 0xB=Trunk).
   - Cross-checked against the PortaPack **Mayhem** `ui_keyfob` Subaru encoder
     for field order and checksum. Reimplemented in our own style as a
     calibrated, Auto-safe decoder driving the FOBback rollback profiles.

10. **Pandora DXL (alarm firmware)** — automotive RKE framing reference
   - Upstream field-layout and checksum/CRC background for several OOK-PWM RKE
     framings (Mazda Siemens-VDO, VAG ID48, Hyundai/Kia RIO, Santa Fe TRW),
     cross-referenced against the ProtoPirate implementations above and
     reimplemented here as checksum-gated decoders.

11. **rtl_433 (merbanan)** — ISM-band device decoder collection
   - Documented Honda KR5V2X/KR5V1X keyfob frame (2-FSK Manchester ~60/120 µs,
     `EC 0F 62` manufacturer preamble, `[idx][deviceID32][event][counter24]
     [rolling32]` payload, OpenSafety CRC-8 poly 0x2F init 0x00 over the payload)
     — reimplemented in our own style as a calibrated, Auto-safe decoder.
   - Broad reference for the automotive-remote landscape (GM ABO1502T, Chrysler,
     Ford, Continental, Siemens, HCS361/HCS362 KeeLoq framings).

---

## Datasheets & Technical Documentation

1. **TI CC1101 Datasheet (SWRS061)** — Single-Chip Low-Cost Low-Power Sub-1 GHz RF Transceiver
   - Register configurations, PATable values, AGC settings
   - Table 25: PATable power levels

2. **Microchip AN1064** — KeeLoq Code Hopping Decoder
   - KeeLoq algorithm specification, NLFSR structure
   - Key derivation functions, learning modes

3. **Microchip HCS301 Datasheet** — KeeLoq Hopping Code Encoder
   - Frame structure, preamble timing, sync counter

3a. **Microchip HCS362 Datasheet (DS40189)** — KeeLoq Code Hopping Encoder
   - 69-bit stream (32-bit hopping + 37-bit fixed: 28/32-bit serial, function,
     status, 2-bit CRC, 2-bit queue); TE 100/200/400/800 µs; used to scope the
     modern-Toyota (HCS362) framing variant.

4. **ISO/IEC 14443/15693/18092** — RFID/NFC standards
   - Hitag2 protocol specifications

5. **SAE J2534** — Vehicle diagnostic communication standard

6. **NXP PCF7936/PCF7952 Datasheets** — Transponder ICs
   - Proprietary cipher specifications

---

## Conference Talks & Community Research

1. **DEF CON** — Automotive security presentations
   - RKE attack demonstrations
   - RollJam live demonstrations

2. **Chaos Communication Congress (CCC)** — Car security research
   - VW/Audi immobilizer attacks
   - KeeLoq key extraction

3. **Black Hat** — Automotive hacking sessions
   - Challenge-response protocol analysis
   - Side-channel attack techniques

4. **Hardwear.io** — Hardware security workshops
   - Fault injection on RKE MCUs
   - Power analysis on embedded crypto

5. **RTL-SDR Community** — Software-defined radio research
   - Signal capture techniques
   - Frequency analysis methods

6. **GreatScottGadgets Community** — RF hardware research
   - CC1101 optimization
   - Custom preset development

---

## Key Databases

1. **KeeLoq Manufacturer Keys** — 73 entries curated from:
   - HiennNek/non-flipper-rolling-code-support
   - Unleashed firmware key tables
   - Mayhem firmware databases
   - ARF firmware key collections
   - Field research and leaked databases

2. **Hitag2 Known Keys** — 16 entries including:
   - Factory defaults
   - ASCII patterns
   - Renault V1 brute-force targets
   - Fiat V1 BCM keys

3. **VAG AUT64 Keys** — 3 hardcoded keys:
   - VW-2, VW-3 fixed global master keys
   - TEA key schedule for Type 2

4. **Protocol Timing Database** — 23 protocols:
   - te_short, te_long, te_delta, min_count_bit
   - PT2262, EV1527, CAME, FAAC, Sec+, and more

---

## Standards & Specifications

1. **NIST FIPS 197** — AES-128 specification
2. **RFC 7748** — TEA/XTEA block cipher
3. **CC1101 Application Notes** — TI documentation
4. **Flipper Zero SDK Documentation** — FAP development guidelines
5. **Furi OS Documentation** — Threading, mutex, event flag APIs
6. **Flipper Zero firmware ELF loader** (official tree, 1.3.3 / 1.3.3-rc,
   `lib/toolbox/elf_file.c`)
   - Each `SHF_ALLOC` section is allocated from one free heap block, with a
     margin added to the section size. That check is why the current FAP
     defers the radio, shares decoder scratch, and leaves the dashboard and
     the full clone catalog out of the linked image.

---

## In-house measurements (not an external source)

These results are ours. They are listed here so a reader can tell them apart
from the papers and repositories above.

1. **Corpus honesty report** — `source/fobworks_flipper/CORPUS_HONESTY.md`
   - `tools/sub_check --report` on the private 162-file `.sub` set, re-run
     24 Sep 2026.
   - Auto 11 (6.8%): Fiat-V2 (4), BMW-CAS3-PPM (2), KeeLoq-HCS300 (2),
     KIA/Hyundai (2), Suzuki (1). Force-only 144 (88.9%). No decode 7 (4.3%).
   - The report file has protocol names and counts. It does not include
     filenames, serials, hopping codes, or keys.

2. **BMW CAS3/CAS4 PPM gate** — structural read in `protocol/flipper_oem_wire.c`
   - Constant-mark PPM (~250 µs HI, ~500/1500 µs LO) accepted only after a
     ≥10 ms sync pair and a ≥64-bit run. Payload is treated as opaque (AES).
     Calibrated against in-house X5 captures. No external decoder is copied
     for this path.

3. **Verdict card and `.sub` tags** — `protocol/flipper_verdict.c`
   - Confidence (`Auto` / `Force` / `None`) and one next action, written back
     as `FT_Confidence` and `FT_Action` on a stock-RAW `.sub`.
   - Preset hint (`try OOK` / `try 2FSK`) from edge ratios in the same capture.
