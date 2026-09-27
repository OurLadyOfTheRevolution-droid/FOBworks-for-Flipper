# Corpus honesty

Public report from `tools/sub_check --report`. Re-run 24 Sep 2026 on the
private capture folder. The tool counts `.sub` files only.

This file has folder totals and protocol names. It does not list filenames,
serials, hopping codes, or keys.

| Outcome | Count | Share | What it means on the Flipper |
|---------|------:|------:|------------------------------|
| Auto | 11 | 6.8% | Fires in normal FOBscan use |
| Force-only | 144 | 88.9% | Decodes only when that protocol is selected |
| No decode | 7 | 4.3% | No decoder accepted the burst |

Auto protocols in that run:

| Protocol | Files |
|----------|------:|
| Fiat-V2 | 4 |
| BMW-CAS3-PPM | 2 |
| KeeLoq-HCS300 | 2 |
| KIA/Hyundai | 2 |
| Suzuki | 1 |

Full histogram from the same run. A trailing `*` is force-only.

| Protocol | Files |
|----------|------:|
| KeeLoq* | 80 |
| Holtek-HT6P20* | 23 |
| PT2262* | 19 |
| FAAC-SLH* | 8 |
| Toyota* | 7 |
| Fiat-V2 | 4 |
| DoorHan-Rolling* | 3 |
| TPMS* | 3 |
| BMW-CAS3-PPM | 2 |
| KeeLoq-HCS300 | 2 |
| KIA/Hyundai | 2 |
| Security+2.0* | 1 |
| Suzuki | 1 |

Folder bucket (one directory):

| Folder | Auto | Force | None |
|--------|-----:|------:|-----:|
| keeloq_test_subs | 11 | 144 | 7 |

The seven no-decode files stay no-decode. They need a cleaner capture or a
stronger checksum before they join Auto. They are not promoted on a
structural guess.

## Why this number is the product

A Sub-GHz app that Auto-labels most of a mixed automotive folder is usually
claiming frames it did not actually validate. On this set the honest Auto
rate is 6.8%. The other decodes are still in the app, behind an explicit
force, so a researcher can ask for them. Normal use does not.

FOBscan shows the same split on the device: `Auto`, `Force`, or `None`, plus
one next action. Saved `.sub` files stay openable in the stock Sub-GHz app
and carry `FT_Confidence` and `FT_Action` for this result.
