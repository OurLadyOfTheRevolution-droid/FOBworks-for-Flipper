FOBworks for Flipper — FAP and source
=============================================

My bundled app version is 1.4. This ZIP includes the M4 build I confirmed on
my Flipper.

I confirmed the FOBclone and FOBcatch Make menus. I have not separately
recorded a new FOBback check or qualified every capture and transmission flow.
I kept the confirmed working FAP unchanged.

Official firmware 1.4.2 / 1.4.3 (FAP target 7, API 87.1) is the supported runtime. A FAP loads only on an exact API match, so this build does not run on 1.3.x (API 86.0).

Contents
--------
source/fobworks_flipper/dist/fobworks_flipper.fap
  Flipper Zero application. I copy it to:
  SD:/apps/Sub-GHz/fobworks_flipper.fap
  I reboot after replacing an older copy. Catalog and Scher-Khan plugins are already packed inside this one file; I do not install standalone .fal files.

source/fobworks_flipper/
  FAP source, application.fam (host + embedded plugins), protocol modules, scenes, and host tests. From this directory, I build with `ufbt`. See source/fobworks_flipper/README.md for the build steps.

Notes
-----
The manufacturer-key table is stored in masked form. The app unmasks each value when it uses the table. FOBLoq displays the stored masked value; it does not show the keys in plaintext. `tools/mask_mfrkeys.py` reverses the masking.

PROMO.md
  Short description of the app, menu, and what this release includes.

SIZE_BASELINE.md
  Loader section sizes for the host FAP and the embedded plugins.

source/fobworks_flipper/CORPUS_HONESTY.md
  Private 162-file capture set by protocol and count, without filenames, serials, hopping codes, or keys. Re-run 24 Sep 2026: Auto 11 (6.8%), force-only 144 (88.9%), no decode 7 (4.3%).

source/deliverables/fobscan-classification-corpus/
  Synthetic decode/classification corpus: 1500 `.sub` files across 13 protocols, with `manifest.csv` and `SUMMARY.txt`. All signals are generated; the corpus contains no real vehicle or gate captures. I regenerate or check:

      cd source/fobworks_flipper/tools
      make corpus            # writes into ../../deliverables/...
      make test              # measures only, writes nothing

  `make test` runs the generator in `--check` mode for Auto (`flipper_decode`, `auto_safe` only) and forced (`flipper_decode_ex`) paths. A force-only protocol's 0% Auto score is registry policy, not a broken decoder.

Known issues
------------

Security+1.0 uses the public ternary/OOK format (42 symbols over two packets, no checksum) from the secplus reference implementation, Flipper `secplus_v1.c`, and rtl_433. Forced decode recovers rolling and fixed fields from a frame built to that specification. Auto still skips it: no transmitted checksum, live false-positive rate unmeasured.

Security+2.0 uses Manchester plus the public ORDER/INVERT scramble. Force-only for the same Auto policy reason until I measure live false positives.

Honda KR5 Manchester marks sit near 60 µs. The capture floor is 40 µs so those edges reach the decoder.

The dashboard link requires a random 6-digit access code on the Flipper and `AUTH:<BRIDGE_KEY>` on the Wi-Fi bridge before commands forward.

Architecture
------------
The host FAP is slim EXTERNAL. Vehicle/year tables live in `fw_catalog.fal`. Sec+ 1.0/2.0, Scher-Khan, Hitag2, Mazda, Honda (incl. KR5), Toyota, Nissan, and PSA Mode 0x23 live in `fw_force.fal`. Both are `fal_embedded` under `/assets/plugins/`, mapped when FOBclone / FOBcatch / FOBback / Force need them, and unmapped on the main menu. Advanced Settings picks the built-in or an OTG CC1101; external presence is confirmed with `subghz_devices_is_connect` on `cc1101_ext` after OTG, not OTG power alone.

Loader sizes for this host image (limits 61864 / 16621 / 5924):
  .text 61088, .rodata 15396, .bss 5199.

SHA-256 of the copy in this repository:
9fe94b310be29f1ec60c2c4e4ef9aa4f6be84cbafe30d5829784fbaa5bf970b1

The binary attached to a GitHub release is built by the "Build FAP" workflow from this source. The build is not byte-reproducible, so digests drift between runs. Target/API alone is NOT proof of equivalent contents. The workflow also uploads SHA256SUMS and BUILD_MANIFEST.json with the exact built FAP. After any local build, I run:
  python3 tools/sync_release.py --sync
  python3 tools/sync_release.py --check
This synchronizes FAP/ and dist/, updates this hash and the size baseline, and rejects stale scene labels or over-budget sections.

LOCAL MAINTENANCE UPDATE
------------------------
Dashboard Link always requires a random six-digit LinkAuth code; the code is visible on Advanced Settings. Remote auth changes require exactly six digits and cannot clear the gate. WiFi bridge defaults are empty and fail closed; I configure unique credentials locally before flashing. Outbound UART data now goes only to authenticated sockets.

Plugin loading/unloading and decoder execution share a recursive lifetime guard; decoder and TE-estimator scratch use the same serialization.

FOBfreq compares coarse TE timing in microseconds, within the same decoded protocol. Four samples per profile are required. Its 32-us bins do not measure RF carrier offset, crystal ppm, transmitter identity, or clones.

FOBtrack uses Left to alternate RKE / force-only TPMS receive modes at the same selected frequency, preserving the last 64 observations. RKE mode accepts rolling decoded frames only. Full logs evict the oldest record. OK resets the session. TPMS remains structural/unverified and is NOT promoted into Auto. Correlation is temporal co-occurrence, not vehicle ID.

FOBreport labels its grade as an observation, leaves counter width unknown, and does not equate locally predicted ranges with receiver resync windows. See LOCAL_CHANGES.md for verification and on-device checks I still need.

I couldn't have built this without the hard work of these developers. I list the repositories, protocols, datasheets, and measurements I relied on in CITATIONS_AND_REFERENCES.md. These are the Saints of The Chapel of Our Lady Of The Revolution.

Please check them out, follow, and support their work!

— OurLadyOfTheRevolution-droid