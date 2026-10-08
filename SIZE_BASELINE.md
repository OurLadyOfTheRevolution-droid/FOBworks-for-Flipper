# FAP size baseline — official SDK 1.4.3, Target 7, API 87.1

I generated these figures from the built images with tools/sync_release.py.
These sizes do not tell me whether heap and stack usage are safe on hardware.

| Host section | Bytes | Recorded budget | Headroom |
|---|---:|---:|---:|
| `.text` | 61852 | 61864 | 12 |
| `.rodata` | 15480 | 16621 | 1141 |
| `.bss` | 5199 | 5924 | 725 |

| Embedded plugin | .text | .rodata | .data | .bss | Resident sections |
|---|---:|---:|---:|---:|---:|
| `fw_catalog.fal` | 308 | 5968 | 1220 | 0 | 7496 |
| `fw_force.fal` | 7784 | 897 | 0 | 2560 | 11241 |

Embedded assets: 29364 bytes.

Host SHA-256: `039d75e9d37c83237d295b7503a9fe43e28b6448650196dd9be7e8803ae96ce5`.
