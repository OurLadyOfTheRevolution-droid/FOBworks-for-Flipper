# FAP size baseline (official SDK 1.4.3, target f7, API 87.1)

I measured these with `ufbt` against
`https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip`
after parking Toyota/Nissan with Sec+/Scher-Khan/Hitag2 in `fw_force.fal`
and minting a random Link Auth code on enable.

Host FAP (`fobworks_flipper.fap`) — what the loader maps at launch:

| Section  | This build | Loader limit | Headroom |
|----------|-----------:|-------------:|---------:|
| `.text`  |       TBD |        61352 |     TBD |
| `.rodata`|       TBD |        17645 |     TBD |
| `.bss`   |       TBD |         5924 |     TBD |

Embedded plugins (mapped on demand, unmapped on the main menu):

| FAL              | `.text` | `.rodata` | Role |
|------------------|--------:|----------:|------|
| `fw_catalog.fal` |     TBD |       TBD | Full FOBclone/FOBcatch/FOBback year table |
| `fw_force.fal`   |     TBD |       TBD | Sec+, Scher-Khan, Toyota, Nissan, Hitag2 |

`.fapassets` in the host FAP is 22308 bytes (the two FALs packed for
single-file distribute). That is flash, not host RAM, until a plugin is
mapped.

APPCHK: Target 7, API 87.1 for host and both plugins. Force plugin ABI is 2.

— OurLadyOfTheRevolution-droid
