# Local review fixes

Lab notes for the tree after `7846e1c`. Bridge fail-closed, link auth, plugin
lifetime, FOBfreq/FOBtrack/FOBreport honesty, release sync, and host fixtures.
I did not extend transmit, jamming, key recovery, or rolling-code prediction.

## What changed

- Bridge: empty defaults fail closed; local config ignored until set;
  authenticated inbound and outbound routing; reconnect clears auth;
  invalid-client bounds.
- FAP control link: mandatory random six-digit code whenever a link starts;
  show/rotate it on device. Reject empty, partial, overlong, or non-digit
  replacements before touching the live code. Code generation serialized
  against command dispatch.
- Plugin lifetime: init before workers; recursive guard across decode and
  shared TE scratch; unload waits for active borrowers. Clear submenu labels
  before unmapping the catalog.
- FOBfreq: microseconds, not fabricated ppm; minimum four samples; estimator
  resolution stated; no transmitter identity claim.
- FOBtrack: RKE/TPMS receive switching keeps observations; rolling RKE
  filter; bounded sliding window instead of stopping at 64; reset action;
  structural/co-occurrence caveats only.
- FOBreport: no inferred counter width; no receiver-window or security claim
  from locally predicted ranges.
- Distribution: SDK FAP synced into `FAP/` and `dist/`; fresh SHA256SUMS,
  BUILD_MANIFEST, and size baseline. Manual Build FAP workflow uploads
  checksums/manifest with the asset.
- Host fixtures for the production bridge sketch, plugin lifetime, code
  validation, timing inputs, and rolling-log overflow. Bounded string-copy
  cleanups keep prior output shape and pass strict optimizing GCC hosts.

## Build and checks

Official SDK **1.4.3**, hardware Target **7**, API **87.1**, uFBT **0.2.6**.
SDK archive digest is pinned in the workflow and build manifest.

From `source/fobworks_flipper`, run `ufbt` with that SDK.
From the repository root:

```sh
make -C source/fobworks_flipper/tools test
python3 tools/sync_release.py --sync
python3 tools/sync_release.py --check
sha256sum -c SHA256SUMS
```

Host regressions use simulated signals. Forced classification is not proof of
live RF decode. Heap, stack, UI concurrency, other firmware branches, and
bridge electrical behavior still need hardware.

Local host suite with `-O2 -Wall -Wextra -Werror` (C99 / C++17; C11 for the
pthread fixture): Link 67; library contract 51; TX ownership 21; vehicle
decoder 25; keyvault 14; lab modules 29. Bridge routing and unconfigured
startup fixtures pass; concurrent plugin-unload fixture passes. SDK FAP and
both plugins pass Target 7 / API 87.1. Section budgets, distribution
consistency, and checksums pass. Synthetic corpus: 120 forced labels match;
Auto correct stays at 10% because most of these protocols stay force-only.

## Physical-device check (receive-only)

1. Install the bundled FAP on supported official firmware. Enter/leave the
   main menu and receive scenes repeatedly while a dashboard is attached;
   confirm plugin unload/reload stays stable with no heap failure.
2. Enable Dashboard Link, read the six-digit code; missing/wrong code fails,
   valid code succeeds, blank/partial replacement fails without changing the
   live code. Reconnect after local code rotation.
3. With a configured bridge, connect two sockets. Authenticate only one;
   the other gets no Flipper telemetry or replies. After disconnect/reconnect
   it is unauthenticated again.
4. Collect four timing observations per FOBfreq profile; units are µs; the
   result only describes coarse timing similarity.
5. Alternate FOBtrack modes on the same frequency. The log survives switching,
   continues past 64 observations, and OK clears it. TPMS rows are structural
   candidates, not confirmed sensor or vehicle identity.

## Release note

Pushing `main` does not refresh the GitHub release asset by itself. Upload the
new FAP plus checksum/manifest, or run the Build FAP workflow with publish
left as an explicit manual choice.
