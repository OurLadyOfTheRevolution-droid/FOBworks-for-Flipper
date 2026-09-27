# FOBworks for Flipper

Version 1.3. Official firmware 1.3.3 (FAP target 7, API 87.1).

`dist/fobworks_flipper.fap` goes in `SD:/apps/Sub-GHz/`. Reboot before replacing an older copy. A failed launch fragments the heap.

## Menu

- FOBscan listens and decodes.
- FOBclone takes a make, model, and year, then two decoded presses.
- FOBcatch takes a make, model, and year, waits one second after the radio opens, and stops on a decoded press.
- FOBback walks rollback profiles and counts decoded presses on that frequency.
- FOBsweep shows RSSI.
- FOBprotos lists protocols.
- FOBLoq lists the built-in KeeLoq manufacturer keys. It shows the **stored (masked)** value
  of each key, not the key in the clear: the table ships masked so the source is not a
  plaintext key list. See `tools/mask_mfrkeys.py`.
- FOBwatch receives and saves.
- FOBlabs shows edge timing.
- FOBhunt sweeps the frequency table for signal level.
- FOBcrack opens the radio on OK, drops the first second, and stops on one KeeLoq frame. A listed manufacturer key shows FOUND. Otherwise the screen shows the serial. Up and Down change frequency. OK on the result listens again.
- Library holds saved `.sub` files.
- Settings sets frequency, modulation, squelch, force-protocol, and the dashboard link. The dashboard link starts off.
- FOBpwn records three different Honda presses after consent. A repeated hop does not advance the count.

FOBclone and FOBcatch use the short on-device list: a make and a frequency such as `315 MHz` or `433 MHz`. The year table in `protocol/flipper_vehicles.c` is the full catalog, compiled when `FLIPPER_FAP_SLIM` is unset.

## Decode

Auto runs the decoders marked safe in `protocol/flipper_decoders.c`. Ford on that path is the V0 Manchester frame. A KeeLoq frame needs 66 bits, a hop word, and one function button. The manufacturer-key list runs after that. A frame that matches none of those keys stays KeeLoq with no device key.

Every receiver drops the first second after the radio opens.

## Dashboard

Protocol 1.2, one JSON object per line, on USB and on GPIO pins 13 and 14 at 115200. Both stay down until the dashboard link is on.

The bridge is `../fobworks_wifi_bridge/`. The access point is `FOBworks-Flipper`. The dashboard connects to `ws://192.168.4.1:81`. Set `AP_PASS` in the sketch before flashing. `http://192.168.4.1/` is the bridge page.

`hello` returns protocol 1.2 and the capabilities `status`, `keys`, `scan`, `setfreq`, `squelch`, `capture`, `replay`, `jam`, and `save`.

## Build

```sh
python3 -m pip install ufbt
export UFBT_HOME="$HOME/.ufbt"
cd source/fobworks_flipper
ufbt
```

Official 1.3.3 SDK: `UFBT_HOME=$HOME/.ufbt`.

Loader limits for this image: `.text` 61352, `.rodata` 17645, `.bss` 5924.

```sh
cd tools && make test
```

## Corpus

`CORPUS_HONESTY.md` is the capture histogram. Auto matches are uncommon on that set. A file that fails the decoders stays undecoded.
