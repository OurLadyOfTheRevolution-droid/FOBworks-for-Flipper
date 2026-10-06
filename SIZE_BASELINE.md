# FAP size baseline (official SDK 1.4.3, target f7, API 87.1)

I measured these with `ufbt` against
`https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip`
after moving Security+ into `fw_force.fal`, parking Hitag2 there, wiring the
external CC1101 VERSION probe, and adding dashboard link auth.

Host FAP (`fobworks_flipper.fap`) — what the loader maps at launch:

| Section  | This build | Loader limit | Headroom |
|----------|-----------:|-------------:|---------:|
| `.text`  |      60336 |        61352 |     1016 |
| `.rodata`|      14348 |        17645 |     3297 |
| `.bss`   |       5188 |         5924 |      736 |

Embedded plugins (mapped on demand, unmapped on the main menu):

| FAL              | `.text` | `.rodata` | Role |
|------------------|--------:|----------:|------|
| `fw_catalog.fal` |     312 |      8544 | Full FOBclone/FOBcatch/FOBback year table |
| `fw_force.fal`   |    3172 |       624 | Sec+ 1.0/2.0, Scher-Khan, Hitag2 cipher |

`.fapassets` in the host FAP is 22308 bytes (the two FALs packed for
single-file distribute). That is flash, not host RAM, until a plugin is
mapped.

APPCHK: Target 7, API 87.1 for host and both plugins. Force plugin ABI is 2.

— OurLadyOfTheRevolution-droid
