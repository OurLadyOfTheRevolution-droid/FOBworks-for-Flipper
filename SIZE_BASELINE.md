# FAP size baseline — official SDK 1.4.3, Target 7, API 87.1

I generated these figures from the built images with tools/sync_release.py.
These sizes do not tell me whether heap and stack usage are safe on hardware.

| Host section | Bytes | Recorded budget | Headroom |
|---|---:|---:|---:|
| `.text` | 61464 | 61864 | 400 |
| `.rodata` | 15431 | 16621 | 1190 |
| `.bss` | 5200 | 5924 | 724 |

| Embedded plugin | .text | .rodata | .data | .bss | Resident sections |
|---|---:|---:|---:|---:|---:|
| `fw_catalog.fal` | 308 | 5968 | 1220 | 0 | 7496 |
| `fw_force.fal` | 8304 | 948 | 0 | 2560 | 11812 |

Embedded assets: 30448 bytes.

Host SHA-256: `f8fd00191e02809ab46740d12872cd627ec7ec0f93e678b14446dfab02d36236`.
