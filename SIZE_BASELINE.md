# FAP size baseline (official SDK 1.4.3, target f7, API 87.1)

I measured these with `ufbt` against
`https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip`
after parking Sec+/Scher-Khan/Hitag2/Mazda/Honda/Toyota in `fw_force.fal`,
wiring FOBfreq / FOBtrack / FOBroll scenes, and keeping FOBreport on the host.

Host FAP (`fobworks_flipper.fap`) — what the loader maps at launch:

| Section  | This build | Loader limit | Headroom |
|----------|-----------:|-------------:|---------:|
| `.text`  |      60648 |        61352 |      704 |
| `.rodata`|      15349 |        17645 |     2296 |
| `.bss`   |       5189 |         5924 |      735 |

Host image holds the three lab scenes (FOBfreq, FOBtrack, FOBroll) plus
FOBreport. Mazda, Honda (incl. KR5), and Toyota parsers moved into
`fw_force.fal` behind the same thin stubs used for Sec+/Scher-Khan. Further
host `.text` must still earn every byte — next OEM families stay in FALs.

Embedded plugins (mapped on demand, unmapped on the main menu):

| FAL              | `.text` | `.rodata` | Role |
|------------------|--------:|----------:|------|
| `fw_catalog.fal` |     312 |      8544 | Full FOBclone/FOBcatch/FOBback year table |
| `fw_force.fal`   |    7784 |       896 | Sec+ 1.0/2.0, Scher-Khan, Hitag2, Mazda, Honda, Toyota |

`.fapassets` in the host FAP is 29196 bytes (the two FALs packed for
single-file distribute). That is flash, not host RAM, until a plugin is
mapped.

APPCHK: Target 7, API 87.1 for host and both plugins. Force plugin ABI is 2.

— OurLadyOfTheRevolution-droid
