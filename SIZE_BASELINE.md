# FAP size baseline (official SDK 1.4.3, target f7, API 87.1)

Measured with `ufbt` against
`https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip`
on branch `firmware-review-fixes` after Security+2.0 Manchester + scramble,
preset quarantine, radio version helper, and Land Rover predict honesty.

| Section  | This build | Loader limit | Headroom |
|----------|-----------:|-------------:|---------:|
| `.text`  |      61100 |        61352 |      252 |
| `.rodata`|      17422 |        17645 |      223 |
| `.bss`   |       5104 |         5924 |      820 |

APPCHK: Target 7, API 87.1.

Security+2.0 alone pushed `.text` over the loader cap (~61804). Fitting it
back required packing the ORDER/INVERT tables, dropping the joined-bit
rebuild, and omitting Scher-Khan from the device image under
`FLIPPER_FAP_SLIM` (host tests still compile the full decoder).

Headroom is too thin for dashboard auth or Hitag2 in the main FAP. Further
OEM growth takes the ProtoPirate-style plugin split (see FIRMWARE_REVIEW.md
and the store note on FAP plugin policy). First candidate: vehicle catalog /
heavy OEM families as `FlipperAppType.PLUGIN` with on-demand load.

— OurLadyOfTheRevolution-droid
