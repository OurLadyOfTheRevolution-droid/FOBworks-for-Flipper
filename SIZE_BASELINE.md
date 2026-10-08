# FAP size baseline — official SDK 1.4.3, Target 7, API 87.1

I generated these figures from the built images with tools/sync_release.py.
These sizes do not tell me whether heap and stack usage are safe on hardware.

| Host section | Bytes | Recorded budget | Headroom |
|---|---:|---:|---:|
| `.text` | 58840 | 61864 | 3024 |
| `.rodata` | 15197 | 16621 | 1424 |
| `.bss` | 5200 | 5924 | 724 |

| Embedded plugin | .text | .rodata | .data | .bss | Resident sections |
|---|---:|---:|---:|---:|---:|
| `fw_catalog.fal` | 308 | 5968 | 1220 | 0 | 7496 |
| `fw_force.fal` | 11520 | 1200 | 0 | 3584 | 16304 |

Embedded assets: 35732 bytes.

Host SHA-256: `6d207a0d4629f8859d653ea4d752a81075e5adb6c9bfcebd4682365333919a5b`.
