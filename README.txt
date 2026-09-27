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
