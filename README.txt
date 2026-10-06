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

  Security+1.0 is force-only (no transmitted checksum). It is included in the
  forced path figures and excluded from Auto; see “Known issues” below.

Known issues
------------

Security+1.0 now uses the public ternary/OOK format (42 symbols over two
packets, no checksum) from argilo/secplus, Flipper `secplus_v1.c`, and
rtl_433. Forced decode recovers rolling and fixed fields from a frame built
to that specification. Auto still skips it: the protocol has no checksum, so
the false-positive rate on live captures is unmeasured. Keep it selected
explicitly until a real capture set is checked.

Honda KR5 Manchester marks sit near 60 µs. The capture path used to drop any
edge shorter than 75 µs, so those frames never reached the decoder. The floor
is 40 µs.

The dashboard link still has no access code. The SGP firmware generates a
per-device code and rejects unauthorized commands. Anyone on the Wi-Fi
bridge AP can still send radio commands to this FAP.

CITATIONS_AND_REFERENCES.md
  Lists the papers, repositories, datasheets, and in-house measurements
  referenced by the decoders and corpus report.

FIRMWARE_REVIEW.md
  Notes from a pass over this FAP: Security+1.0 format, vault replace,
  40 µs RX floor, owned sequence TX buffers, and what is still open.

source/fobworks_wifi_bridge/
  Optional ESP32 bridge source. The bundled FAP does not link the
  USB/UART dashboard, so the bridge has no on-device peer in this build.

What this FAP is
----------------
Menu labels put the tool name first, as in FOBcatch (rolljam) and FOBback
(rollback). The other hubs follow the same pattern. After each burst, FOBscan
shows a verdict with the protocol, Auto/Force/None status, and a next action.

To fit within the official 1.4.3 loader limits, the FAP defers CC1101 setup
until a radio scene, shares decoder scratch space, and includes one Generic
KeeLoq row in FOBclone. FOBback retains its guided profiles, beginning with
FOBpwn (Honda). Hitag2 is not included in this FAP. The full vehicle table and
JSON protocol remain in the source tree for host tests.

The JSON dashboard link is compiled in and reachable from Settings -> Dashboard
Link; it is off by default, which saves about 9 KB of heap at launch. Both USB
and UART transports are allocated when it is switched on. Loader sizes for this
image are `.text` 60328, `.rodata` 17437, `.bss` 5105 against limits of 61352,
17645 and 5924.

`link/flipper_link_stub.c` has been removed. It was never part of the build (absent
from the `sources` list in `application.fam`) and redefined `flipper_link_alloc` and
its neighbours, so adding it to the build would have produced duplicate symbols
rather than a smaller image. Its header described a loader constraint that did not
apply to the source tree as shipped.

SHA-256 of the copy in this repository:
bd4f8e3059a26d648fad92b0bf1678eca13313f88df396a1ddac544708b10ec9

The binary attached to the v1.3 release is built by the "Build FAP" workflow
from this same source on a GitHub runner. Its digest is not recorded here: the
build is not byte-reproducible, so the workflow produces a differently arranged
binary each time it runs and any hash written down goes stale at the next run.
The two are the same application. Compare the API and target version instead
(both report Target 7, API 87.1), or run the workflow to build one yourself.

Without the hard work of these developers, this project would not be possible.
The specific repositories, protocols, data, and other info we relied on for this
project are described in more detail in the CITATIONS_AND_REFERENCES document.
These are the Saints of The Chapel of Our Lady Of The Revolution.

@D4C1-Labs
@RocketGod-git
@HiennNek
@ArtGudvin
@merbanan
@tomwimmenhove
@DarkFlippers

Please check them out, follow, and support their work!
