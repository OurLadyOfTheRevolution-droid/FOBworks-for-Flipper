# FOBworks for Flipper

I identify this release as version 1.4 in the source manifest and bundled FAP.
Official firmware 1.4.2 / 1.4.3 (FAP target 7, API 87.1) is the supported
runtime. A FAP loads only on an exact API match, so this build does not run on
1.3.x (API 86.0).

Copy `dist/fobworks_flipper.fap` to `SD:/apps/Sub-GHz/`. Reboot before
replacing an older copy; a failed launch can fragment the heap. Catalog and
Scher-Khan plugins are already packed inside that one FAP.

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
- **FOBreport** is a read-only health check. Press your fob a few times and it
  grades the remote (A = encrypted rolling, D = fixed/replayable) without ever
  transmitting.
- **FOBfreq** compares two TE-proxy crystal fingerprints (profiles A/B; Left
  switches). Same protocol only; read-only.
- **FOBtrack** scores TPMS↔RKE co-occurrence from ambient captures. Set Force
  Proto to TPMS when sampling tire sensors. Read-only.
- **FOBroll** analyzes consecutive rolling-code counters for a generalized
  RollBack candidate (Csikor et al.). Read-only; it never transmits.
- **Library** contains saved `.sub` files.
- **Settings** controls frequency, modulation, squelch, force-protocol, and
  the dashboard link. The link is off by default.
- **FOBpwn** counts three different Honda presses after consent. A repeated
  hop does not increase the count.

FOBclone, FOBcatch, and FOBback map `fw_catalog.fal` for the full
make/model/year table in `protocol/flipper_vehicles.c`. The host FAP does not
keep that table in its own `.rodata`. Returning to the main menu unmaps the
catalog. Force → Scher-Khan / Sec+ / Mazda / Honda / Toyota maps
`fw_force.fal`.

## Decode

Auto runs the decoders marked safe in `protocol/flipper_decoders.c`. Ford on
that path uses the V0 Manchester frame. KeeLoq recognition requires a 66-bit
frame, a hop word, and one function button. The app then checks the
manufacturer-key list. If no key matches, the frame remains classified as
KeeLoq, with no device key.

Security+1.0, Security+2.0, and Scher-Khan stay force-only.

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

Loader limits for the host image: `.text` 61352, `.rodata` 17645, `.bss` 5924.
See `SIZE_BASELINE.md` for the numbers on this tree.

```sh
cd tools && make test
```

## Corpus

`CORPUS_HONESTY.md` reports the capture histogram. Auto matches are uncommon
in that set; files that none of the decoders accepts remain undecoded.

— OurLadyOfTheRevolution-droid
