# FAP size baseline (official SDK 1.4.3, target f7, API 87.1)

I measured these with `ufbt` against
`https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip`
on branch `firmware-review-fixes` after the plugin split (v1.4 host FAP).

Host FAP (`fobworks_flipper.fap`) — what the loader maps at launch:

| Section  | This build | Loader limit | Headroom |
|----------|-----------:|-------------:|---------:|
| `.text`  |      60648 |        61352 |       92 |
| `.rodata`|      14236 |        17645 |     3409 |
| `.bss`   |       5188 |         5924 |      736 |

Embedded plugins (mapped on demand, unmapped on the main menu):

| FAL              | `.text` | `.rodata` | Role |
|------------------|--------:|----------:|------|
| `fw_catalog.fal` |     312 |      8544 | Full FOBclone/FOBcatch/FOBback year table |
| `fw_force.fal`   |     448 |        96 | Scher-Khan Magicar PWM (force-only) |

`.fapassets` in the host FAP is 17648 bytes (the two FALs packed for
single-file distribute). That is flash, not host RAM, until a plugin is
mapped.

APPCHK: Target 7, API 87.1 for host and both plugins.

`.rodata` headroom is back. `.text` is still tight. Further OEM growth goes
in a FAL, not the host image.

— OurLadyOfTheRevolution-droid
