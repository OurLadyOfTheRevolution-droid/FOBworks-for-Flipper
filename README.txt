FOBworks for Flipper — current FAP and source
=============================================

Contents
--------
source/fobworks_flipper/dist/fobworks_flipper.fap
  Current Flipper Zero application. Copy to:
  SD:/apps/Sub-GHz/fobworks_flipper.fap
  v1.1. Verified to launch on official firmware 1.3.3 (FAP target 7, API 87.1).

source/fobworks_flipper/
  FAP source, application.fam, protocol modules, scenes, and host-test
  source. Build with `ufbt` from this directory.
  README: source/fobworks_flipper/README.md


Notes
-----
The manufacturer key table is stored MASKED. The app inverts it at each use, so behaviour is
unchanged, but the source and the built .fap no longer contain the keys in the clear.
FOBLoq shows the stored (masked) value on screen. tools/mask_mfrkeys.py reverses it.

PROMO.md
  What the app is, the menu names, and what is new for the Flipper community.

source/fobworks_flipper/CORPUS_HONESTY.md
  Public decode report for the private 162-file capture set. Protocol names
  and counts only. Re-run 24 Sep 2026: Auto 11 (6.8%), force-only 144
  (88.9%), no decode 7 (4.3%).

source/deliverables/fobscan-classification-corpus/
  Synthetic decode/classification corpus: 1500 .sub files over 13 protocols,
  plus manifest.csv and SUMMARY.txt. Every signal is generated, so it holds no
  real vehicle or gate data. Regenerate or measure with:

      cd source/fobworks_flipper/tools
      make corpus            # writes into ../../deliverables/...
      make test              # measures only, writes nothing

  `make test` runs the generator in --check mode and reports decode and
  classification accuracy on both the Auto path (flipper_decode, auto_safe
  decoders only) and the forced path (flipper_decode_ex after selecting a
  protocol). A force-only protocol scoring 0% on Auto is registry policy, not a
  decoder failure.

  Security+1.0 is reported separately as KNOWN-BROKEN and excluded from the
  headline figure. See "Known issues" below.

Known issues
------------

Security+1.0 does not decode. `flipper_decode_secplus1` implements a 40-bit
binary model with a 4-bit popcount checksum. The real protocol is 42 TERNARY
symbols -- BIT_0/1/2 are 3T/2T/1T low pulses -- spread over two packets, with no
checksum field at all. Reference: Flipper-ARF
`lib/subghz/protocols/secplus_v1.c`, which contains both an encoder and a
decoder. A frame built to that spec is refused.

The ~3% that do pass are chance matches of the checksum gate, not decodes. This
is why the corpus row is excluded from the accuracy total rather than averaged
in: a broken decoder should not be able to hide inside a headline number.

Fixing it means rewriting the decoder to the ternary spec. That needs a real
capture to validate against, and the generator cannot produce one, because there
is no checksum to compute -- generating frames that satisfy the current decoder
would only make a wrong decoder look correct.

Measured: across 180 synthetic signals from the other twelve protocols, the
Auto path never mis-attributed one to Sec+ 1.0, so its false-positive risk on
this corpus is low. It remains registered auto_safe in FLIPPER_DECODERS, which
is the one place the app still presents it as working.

CITATIONS_AND_REFERENCES.md
  Papers, repositories, datasheets, and the in-house measurements behind
  the decoders and the corpus report.

source/fobworks_wifi_bridge/
  Optional ESP32 bridge source. The current FAP does not link the USB/UART
  dashboard, so this bridge has no on-device peer in this build.

What this FAP is
----------------
Menu labels lead with the tool name (FOBcatch (rolljam), FOBback (rollback),
and the same pattern on the other hubs). FOBscan ends each burst on one
verdict: protocol, Auto/Force/None, and one next action.

The image fits official 1.3.3 by deferring the CC1101 until a radio scene,
sharing decoder scratch, linking a link stub instead of the JSON dashboard,
and shipping one Generic KeeLoq row in FOBclone. FOBback still has its
guided profiles, starting at FOBpwn (Honda). Hitag2 is not in this FAP.
The full vehicle table and the JSON protocol stay in the tree for host tests.

FAP SHA-256:
c963cb239d7409b72c4c565f57ccdd9621922dba5e7d8255b250bd120fa2a7c3
