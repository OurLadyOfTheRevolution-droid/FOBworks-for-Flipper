# Local review fixes

This package is based on the downloaded `main` source at `7846e1c`.
No commits, pushes, tags, workflow dispatches, or release uploads were
performed on GitHub. The published v1.4 asset is therefore still unchanged.

## Included changes

- Bridge: fail-closed empty defaults, ignored local configuration,
  authenticated inbound **and outbound** routing, reconnect reset and
  invalid-client bounds.
- FAP control link: mandatory random six-digit code whenever links start;
  show/rotate it locally. Reject empty, partial, overlong or non-digit
  replacement codes before modifying the current code. Code generation is
  serialized against command dispatch.
- Plugin lifetime: initialize before workers; hold a recursive guard across
  decoding and shared TE scratch; unloading waits for active borrowers.
  Clear submenu labels before unmapping their catalog.
- FOBfreq: microseconds, not fabricated ppm; minimum four samples, estimator
  resolution acknowledged, no transmitter/crystal identification claim.
- FOBtrack: explicit RKE/TPMS receive switching preserves observations;
  rolling RKE filter, bounded sliding window instead of silently stopping at
  record 64, reset action and structural/co-occurrence caveats.
- FOBreport: no inferred counter width, no receiver-window/security claim
  from locally predicted ranges.
- Distribution: SDK-built FAP synchronized in `FAP/` and `dist/`; fresh
  SHA256SUMS, BUILD_MANIFEST and size baseline. Future manual build workflow
  uploads checksums/manifest with its asset.
- Host fixtures and regression coverage for the production bridge sketch,
  plugin lifetime, code validation, timing inputs and rolling-log overflow.
  Bounded string-copy cleanups retain existing output behavior and permit
  strict optimizing GCC host builds.

No transmit, jamming, cryptographic recovery or rolling-code prediction
behavior was extended.

## Build and checks

Official SDK **1.4.3**, hardware Target **7**, API **87.1**, uFBT **0.2.6**.
The SDK archive digest is pinned in the workflow and build manifest.

From `source/fobworks_flipper`, run `ufbt` with that SDK.
From the repository root:

```sh
make -C source/fobworks_flipper/tools test
python3 tools/sync_release.py --sync
python3 tools/sync_release.py --check
sha256sum -c SHA256SUMS
```

Host regressions use simulated signals: forced classification is not proof
of successful live RF decoding. Runtime heap, stack, UI concurrency, SDK
compatibility on other firmware branches and bridge electrical behavior
need actual hardware.

Verified locally: the complete host suite passes with explicit optimizing
GCC flags (`-O2 -Wall -Wextra -Werror`, C99 / C++17; C11 for the pthread
fixture). Link: 67 checks; library contract: 51; TX ownership: 21; vehicle
decoder: 25; keyvault: 14; lab modules: 29. Production bridge routing and
unconfigured-startup fixtures pass; concurrent plugin-unload fixture passes.
The SDK FAP and both plugins pass Target 7 / API 87.1 checks. All recorded
section budgets, distribution consistency and checksum checks pass.
Synthetic corpus: 120 forced labels match; Auto correct labels remain 10%
because most of these protocols intentionally remain force-only.

## Suggested physical-device check

Use receive-only operation for these checks:

1. Install the bundled FAP on supported official firmware. Enter/leave the
   main menu and receive scenes repeatedly while a dashboard receives data;
   confirm plugin unload/reload remains stable and no heap failure occurs.
2. Enable Dashboard Link, read its six-digit code, verify a missing/wrong
   code fails, valid code succeeds, and blank/partial replacement fails
   without changing the existing code. Reconnect after local code rotation.
3. With an appropriately configured bridge, connect two sockets. Authenticate
   only one: confirm the other receives no Flipper telemetry or replies.
   Disconnect/reconnect and verify it is no longer authenticated.
4. Collect four timing observations per profile; confirm units are us and
   the result only describes coarse timing similarity.
5. Alternate FOBtrack modes on the same frequency. Confirm the log survives
   switching, continues after 64 observations, and OK clears it. Treat TPMS
   readings as structural candidates, not confirmed sensor/vehicle identity.

## Your GitHub update

Copy this ZIP over a clean checkout of the matching base, inspect the diff,
then commit/push yourself. Check `LOCAL_CHANGES.md` and distribution metadata
with the source. The GitHub release does not update just because you push:
upload the new FAP plus checksum/manifest files or manually run the existing
Build FAP workflow. Its publish option remains an explicit manual choice.
If you have newer edits than the stated base, merge these changes rather
than copying over your newer files.
