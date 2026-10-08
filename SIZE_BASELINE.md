# FAP size baseline — official SDK 1.4.3, Target 7, API 87.1

I generated these figures from the built images with tools/sync_release.py.
These sizes do not tell me whether heap and stack usage are safe on hardware.

| Host section | Bytes | Recorded budget | Headroom |
|---|---:|---:|---:|
| `.text` | 61088 | 61864 | 776 |
| `.rodata` | 15396 | 16621 | 1225 |
| `.bss` | 5199 | 5924 | 725 |

| Embedded plugin | .text | .rodata | .data | .bss | Resident sections |
|---|---:|---:|---:|---:|---:|
| `fw_catalog.fal` | 308 | 5968 | 1220 | 0 | 7496 |
| `fw_force.fal` | 8960 | 990 | 0 | 2560 | 12510 |

Embedded assets: 31516 bytes.

Host SHA-256: `9fe94b310be29f1ec60c2c4e4ef9aa4f6be84cbafe30d5829784fbaa5bf970b1`.
