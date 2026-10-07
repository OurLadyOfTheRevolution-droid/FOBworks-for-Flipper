# FAP size baseline (official SDK 1.4.3, target f7, API 87.1)

I measured these with `ufbt` against
`https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip`
after parking Toyota/Nissan with Sec+/Scher-Khan/Hitag2 in `fw_force.fal`
and minting a random Link Auth code on enable.

Host FAP (`fobworks_flipper.fap`) — what the loader maps at launch:

| Section  | This build | Loader limit | Headroom |
|----------|-----------:|-------------:|---------:|
| `.text`  |      59284 |        61352 |     2068 |
| `.rodata`|      14256 |        17645 |     3389 |
| `.bss`   |       5186 |         5924 |      738 |

Embedded plugins (mapped on demand, unmapped on the main menu):

| FAL              | `.text` | `.rodata` | Role |
|------------------|--------:|----------:|------|
| `fw_catalog.fal` |     312 |      8544 | Full FOBclone/FOBcatch/FOBback year table |
| `fw_force.fal`   |    5132 |       707 | Sec+, Scher-Khan, Toyota, Nissan, Hitag2 |

`.fapassets` in the host FAP packs the two FALs for single-file distribute
(catalog + force ≈ 18 KB on disk as standalone `.fal` files). That is flash,
not host RAM, until a plugin is mapped. Force `.bss` is 2560 (scratch arena
for Toyota).

APPCHK: Target 7, API 87.1 for host and both plugins. Force plugin ABI is 2.

— OurLadyOfTheRevolution-droid
