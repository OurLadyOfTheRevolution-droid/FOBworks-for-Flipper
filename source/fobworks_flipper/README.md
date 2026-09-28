# FOBworks for Flipper

The source manifest and bundled FAP identify this release as version 1.3. The
upstream release notes report a successful launch on official firmware 1.4.3
(FAP target 7, API 87.1). A FAP loads only on an exact API match, so this build
runs on 1.4.2 and 1.4.3, not on 1.3.x, whose API is 86.0.

Copy `dist/fobworks_flipper.fap` to `SD:/apps/Sub-GHz/`. Reboot before
replacing an older copy; a failed launch can fragment the heap.

## Menu

- **FOBscan** listens for signals and runs the available decoders.
- **FOBclone** asks for a make, model, and year, then waits for two decoded presses.
- **FOBcatch** asks for a make, model, and year, waits one second after opening the radio, and stops when it decodes a press.
- **FOBback** follows rollback profiles and counts decoded presses on the selected frequency.
- **FOBsweep** displays RSSI.
- **FOBprotos** lists the protocols.
- **FOBLoq** lists the built-in KeeLoq manufacturer keys. The screen shows each
  key's **stored (masked)** value, not the plaintext key. The table is masked
  so the source is not a plaintext key list. See `tools/mask_mfrkeys.py`.
- **FOBwatch** receives and saves signals.
- **FOBlabs** displays edge timing.
- **FOBhunt** scans the frequency table for signal level.
- **FOBcrack** opens the radio when you press OK, waits through the first
  second, then stops on one KeeLoq frame. It displays FOUND for a listed
  manufacturer-key match; otherwise it shows the serial. Use Up and Down to
  change frequency, then press OK on the result to listen again.
- **Library** contains saved `.sub` files.
- **Settings** controls frequency, modulation, squelch, force-protocol, and
  the dashboard link. The link is off by default.
- **FOBpwn** counts three different Honda presses after consent. A repeated
  hop does not increase the count.

FOBclone and FOBcatch use a short on-device list of makes and frequencies
(for example, `315 MHz` or `433 MHz`). The full year table is in
`protocol/flipper_vehicles.c`; it is compiled when `FLIPPER_FAP_SLIM` is unset.

## Decode

Auto runs the decoders marked safe in `protocol/flipper_decoders.c`. Ford on
that path uses the V0 Manchester frame. KeeLoq recognition requires a 66-bit
frame, a hop word, and one function button. The app then checks the
manufacturer-key list. If no key matches, the frame remains classified as
KeeLoq, with no device key.

Receivers skip the first second after the radio opens.

## Dashboard

The dashboard link uses protocol 1.2, with one JSON object per line over USB or
GPIO pins 13 and 14 at 115200 baud. Both transports remain disabled until the
dashboard link is enabled.

The optional bridge source is in `../fobworks_wifi_bridge/`. It creates the
`FOBworks-Flipper` access point, and the dashboard connects to
`ws://192.168.4.1:81`. Set `AP_PASS` in the sketch before flashing the bridge.
The page at `http://192.168.4.1/` is the bridge status page, not the dashboard.

The `hello` response reports protocol 1.2 and these capabilities: `status`,
`keys`, `scan`, `setfreq`, `squelch`, `capture`, `replay`, `jam`, and `save`.

## Build

```sh
python3 -m pip install "ufbt==0.2.6"
export UFBT_HOME="$HOME/.ufbt"
cd source/fobworks_flipper
ufbt
```

That builds against whatever SDK `ufbt` last downloaded. To build against the
release this FAP was built for, pin it first:

```sh
ufbt update --url \
  https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip \
  -t f7
ufbt
```

The build reports `Target: 7, API: 87.1`. A FAP loads only on an exact API
match, so a binary built this way runs on 1.4.2 and 1.4.3, not on 1.3.x.

The build is not byte-reproducible; the linker lays code out differently between
runs, so rebuilds of the same sources have different hashes. Compare the API and
target version, not the digest. `.github/workflows/build-fap.yml` does this on
demand from the public source if you would rather not build locally.

Loader limits for this image: `.text` 61352, `.rodata` 17645, `.bss` 5924.

```sh
cd tools && make test
```

## Corpus

`CORPUS_HONESTY.md` reports the capture histogram. Auto matches are uncommon
in that set; files that none of the decoders accepts remain undecoded.
