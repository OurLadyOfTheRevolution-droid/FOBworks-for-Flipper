# Corpus honesty

I recorded these results with `tools/sub_check --report` on 24 Sep 2026 using my private capture folder. The tool counts `.sub` files only.

I report folder totals and protocol names, not filenames, serials, hopping codes, or keys.

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

The table below gives the full histogram from the same run. A trailing `*` marks a force-only result.

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

The seven files with no decode remain in that category. They need a cleaner capture or a stronger checksum before they can be considered for Auto; a structural guess is not enough to promote them.

## Why this number is the product

Auto labeling is a stricter bar than recognizing a frame after I select a protocol. In this set, 6.8% of files decoded on the Auto path. Other decoders remain available behind an explicit force selection for research; they are not used during normal Auto decoding.

On the device, FOBscan presents the result as `Auto`, `Force`, or `None`, with one next action. The saved `.sub` files remain openable in the stock Sub-GHz app and include `FT_Confidence` and `FT_Action` fields for the result.

## Live Security+ captures

No owned-hardware Security+ 1.0 / 2.0 `.sub` files are checked in yet. The drop folder is `source/deliverables/secplus-live-captures/`. After I record real bursts:

```
cd source/fobworks_flipper/tools && make secplus-live
```

I will paste the Auto / Force / None histogram here. Synthetic SynGate corpus rows stay force-only evidence only; they are not a live false-positive study.

— OurLadyOfTheRevolution-droid