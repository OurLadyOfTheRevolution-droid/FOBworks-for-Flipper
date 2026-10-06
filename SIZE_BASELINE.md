# FAP size baseline (official SDK 1.4.3, target f7, API 87.1)

I measured these with `ufbt` against
`https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip`
after the FOBback lazy-capture fix (catalog maps before the Make list).

Host FAP (`fobworks_flipper.fap`) — what the loader maps at launch:

| Section  | This build | Loader limit | Headroom |
|----------|-----------:|-------------:|---------:|
| `.text`  |      61332 |        61352 |       20 |
| `.rodata`|      14292 |        17645 |     3353 |
| `.bss`   |       5183 |         5924 |      741 |

Embedded plugins (mapped on demand, unmapped on the main menu):

| FAL              | `.text` | `.rodata` | Role |
|------------------|--------:|----------:|------|
| `fw_catalog.fal` |     312 |      8544 | Full FOBclone/FOBcatch/FOBback year table |
| `fw_force.fal`   |     448 |        96 | Scher-Khan Magicar PWM (force-only) |

`.fapassets` in the host FAP is 17648 bytes (the two FALs packed for
single-file distribute). That is flash, not host RAM, until a plugin is
mapped.

APPCHK: Target 7, API 87.1 for host and both plugins.

`.text` headroom is thin (20 B). Further OEM growth stays in a FAL.

— OurLadyOfTheRevolution-droid
