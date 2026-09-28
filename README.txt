FOBworks for Flipper — FAP and source
=============================================

The source and bundled FAP identify this release as version 1.3: the FAP
manifest, source header, and embedded version string agree. The older v1.1
label in the upstream README did not match them. The upstream release notes
report a successful launch on official firmware 1.4.3 (FAP target 7, API 87.1).
A FAP loads only on an exact API match, so this build runs on 1.4.2 and 1.4.3,
not on 1.3.x, whose API is 86.0.

Contents
--------
source/fobworks_flipper/dist/fobworks_flipper.fap
  Flipper Zero application. Copy it to:
  SD:/apps/Sub-GHz/fobworks_flipper.fap

source/fobworks_flipper/
  FAP source, application.fam, protocol modules, scenes, and host tests.
  From this directory, build with `ufbt`. See source/fobworks_flipper/README.md
  for the build instructions.

Notes
-----
The manufacturer-key table is stored in masked form. The app unmasks each value
when it uses the table. FOBLoq displays the stored masked value; it does not
show the keys in plaintext. `tools/mask_mfrkeys.py` reverses the masking.

PROMO.md
  Describes the app, its menu, and the features included in this release.

source/fobworks_flipper/CORPUS_HONESTY.md
  Reports the private 162-file capture set by protocol and count, without
  filenames, serials, hopping codes, or keys. Re-run 24 Sep 2026: Auto 11
  (6.8%), force-only 144 (88.9%), and no decode 7 (4.3%).

source/deliverables/fobscan-classification-corpus/
  Synthetic decode/classification corpus: 1500 `.sub` files across 13
  protocols, with `manifest.csv` and `SUMMARY.txt`. All signals are generated;
  the corpus contains no real vehicle or gate captures. Regenerate the corpus
  or run its checks with:

      cd source/fobworks_flipper/tools
      make corpus            # writes into ../../deliverables/...
      make test              # measures only, writes nothing

  `make test` runs the generator in `--check` mode. It reports decode and
  classification accuracy for the Auto path (`flipper_decode`, using only
  `auto_safe` decoders) and the forced path (`flipper_decode_ex` after a
  protocol is selected). A force-only protocol's 0% Auto score reflects the
  registry policy, not a decoder failure.

  Security+1.0 is marked KNOWN-BROKEN and excluded from the headline figures.
  The explanation is under “Known issues” below.

Known issues
------------

Security+1.0 is not decoded correctly by this build. `flipper_decode_secplus1`
uses a 40-bit binary model with a 4-bit popcount checksum. The protocol instead
uses 42 ternary symbols (`BIT_0/1/2` are 3T/2T/1T low pulses) across two
packets, with no checksum field. The Flipper-ARF reference,
`lib/subghz/protocols/secplus_v1.c`, contains an encoder and decoder; a frame
built to that specification is refused by this decoder.

The roughly 3% of synthetic signals that pass are chance matches to the
checksum gate, not successful decodes. The corpus therefore reports this row
separately instead of including it in the accuracy total.

Fixing the decoder requires implementing the ternary format and validating it
against a real capture. The generator cannot provide that validation: the real
format has no checksum, and generating frames that satisfy the current
checksum-based decoder would only test the wrong format.

In the synthetic corpus, none of 180 signals from the other twelve protocols
was attributed to Sec+ 1.0 by the Auto path. That result applies only to this
corpus. Security+1.0 remains marked `auto_safe` in `FLIPPER_DECODERS`, so the
app can still present the decoder as available despite this known issue.

CITATIONS_AND_REFERENCES.md
  Lists the papers, repositories, datasheets, and in-house measurements
  referenced by the decoders and corpus report.

source/fobworks_wifi_bridge/
  Optional ESP32 bridge source. The bundled FAP does not link the
  USB/UART dashboard, so the bridge has no on-device peer in this build.

What this FAP is
----------------
Menu labels put the tool name first, as in FOBcatch (rolljam) and FOBback
(rollback). The other hubs follow the same pattern. After each burst, FOBscan
shows a verdict with the protocol, Auto/Force/None status, and a next action.

To fit within the official 1.4.3 loader limits, the FAP defers CC1101 setup
until a radio scene, shares decoder scratch space, links a transport stub
instead of the JSON dashboard, and includes one Generic KeeLoq row in FOBclone.
FOBback retains its guided profiles, beginning with FOBpwn (Honda). Hitag2 is
not included in this FAP. The full vehicle table and JSON protocol remain in
the source tree for host tests.

SHA-256 of the bundled FAP as published:
529bad0cf14d1e3aee64b81b7635e457657cce98c24ab4da77cd1bf01edef8aa

The build is not byte-reproducible: the linker lays code out differently
between runs, so a rebuild of these same sources produces a working FAP with a
different digest. The hash above identifies the committed file; it is not a
value you can reproduce locally. Run the "Build FAP" workflow to build your own
from the public source.
