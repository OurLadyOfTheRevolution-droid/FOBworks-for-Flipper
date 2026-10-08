# Local review fixes

## Confirmed Make-menu repair — M4

FOBclone's Make menu appeared with the compact catalog, while FOBcatch still
showed an empty Make screen. After installing M4, I confirmed that FOBcatch's
menu works too. I am recording the menu result, not claiming that every capture,
replay or protocol has been tested end to end.

I found that FOBcatch allocated its full capture state before mapping the
catalog. I now clear the old menu and load the catalog first, so the capture
allocation does not compete with the loader's transient allocations. I kept
FOBcatch's capture and transmission code unchanged.

I use one catalog Make-list builder for FOBclone and FOBcatch. It preserves
the original order and all 30 unique labels, holds the plugin lifetime guard
during enumeration, and respects the output capacity. I checked loading
failures, retry, bounded output and repeated unload/reload through this
production builder.

The successful FOBcatch header is `Catch M4: Make`. An error shows its reason
and an `M4: Back` row. I keep these markers so I can identify the installed
build. I have not separately recorded a new FOBback device check.

I preserved the scanner behavior, app-owned timer, stack sizes and original
memory ceilings. The official SDK build, native checks, release synchronization
and ARM stack-frame checks passed. I replace only the installed FAP and keep
its exact filename; I leave the saved library, settings and keys alone.

## Compact catalog candidate

The diagnostic showed `Catalog: memory` on my Flipper. I traced that result to
the firmware's allocation of plugin sections during preload, before import
resolution or ABI validation.

I replaced padded year/model arrays with exact-length static lists and kept
the vehicle records separate from the strings in the mapped RAM sections.
I preserved all 57 vehicle entries, their year choices, six FOBback make groups,
model names and profiles. I checked every catalog string against the previous
source and exercised every model/year entry through the actual catalog API.

The catalog's resident sections fell from 8,936 to 7,496 bytes. Its largest
section fell from 8,544 to 5,968 bytes. These figures exclude loader metadata
and transient relocation buffers; I have not measured the live device heap.

I changed the internal plugin ABI to 3 because the returned record layouts
changed. I rebuilt the host and both embedded plugins together. The firmware
SDK/API remains official 1.4.3 / 87.1. I kept the original host memory ceiling,
scanner sources, timer ownership and stack sizes unchanged.

FOBclone's menu appeared after this change; the later M4 change restored
FOBcatch's menu. The diagnostic headings remain available if the device cannot
load the catalog. I replace only the installed FAP, keeping its exact filename,
then relaunch it to extract the matching plugins.

## Earlier diagnostic builds

The following notes describe the checks and limitations at each earlier stage.
I keep them as the troubleshooting record, not as the current release status.
The confirmed menu result is recorded above.

### Catalog-load diagnostic

I compared the installed FAP, catalog and force plugin with the previous release.
All three matched byte for byte. I ruled out mismatched or corrupted files, but
I have not yet identified the failure on the device.

I now show the catalog-loading result in the make-picker header instead of
leaving an empty Make screen. I distinguish file/API validation, memory,
missing imports, mapping, ABI checks and an empty catalog.

I checked every model/year entry through the catalog plugin's API in a native
test: 57 vehicle entries and six FOBback make groups. I also checked simulated
loader failures, successful retry and concurrent unload. These checks do not
reproduce the firmware's ARM ELF loader or qualify its available heap.

I preserved the app-owned periodic timer and the scanner's measured stack
frames. I did not increase stack sizes or the original combined memory limit.
This is a diagnostic build, not a confirmed repair of the device's blank menus.

To check it, I replace `apps/Sub-GHz/fobworks_flipper.fap`, keep that exact
filename, launch the app again and open FOBclone. I leave the saved library,
keys and settings alone. The FAP includes its matching plugins.

## App-thread timer candidate

I found that USB logging prevented the immediate crash but blocked sweep input until disconnect; the log has no fault dump. The periodic app callback previously ran on the official SDK's 1,024-byte TimersSrv, with a 424-byte frame before its callees. It now runs on an app-owned periodic event-loop timer, dispatches scene status directly, and I skip dashboard formatting without links. I did not increase stack sizes. See `diagnostics/APP_TIMER_CANDIDATE.md`. Physical crash and sweep-input resolution remain unconfirmed.

## Live-display candidate

I observed the normal autosave-disabled probe also crashed after roughly two seconds, while display-only and RX-only passed. Autosave is therefore not necessary for the reported fault. FOBscan now publishes owned, preformatted text through a locking view model instead of letting the GUI read mutable app state. The GUI draw frame is 40 bytes, down from 208; no thread stack was increased. See `diagnostics/LIVE_DISPLAY_CANDIDATE.md` for evidence, checks and installation. This fixes an observed concurrency defect, but the physical MPU fault remains unconfirmed until I test this candidate on the device.

## MPU-fault follow-up

My first replacement still crashed on the physical device. The original FOBscan fault remains unresolved. I added a compile-time startup probe to separate display-only from receive/decoder-only operation; see `diagnostics/STARTUP_PROBE.md`. The normal build has no probe menu/define.

I install the FAP with the exact name `fobworks_flipper.fap`. The earlier standalone download's suffixed name was a packaging mistake: official 1.4.3 extracts assets by the installed file's basename, while compiled plugin paths use the app ID. The corrected installer ZIP preserves the required basename. This correction does not establish the original crash's cause.

My physical device is on official firmware 1.4.3. I built this FAP for the matching Target 7 / API 87.1, with a 4096-byte app stack. No crash-thread name or PC/LR was available.

I found a definite overflow in that binary: the dashboard command handler reserved 5472 bytes on entry, but both link workers have 4096-byte stacks. I replaced its 4096-byte automatic response buffer with a temporary heap buffer, allocated after authentication under the existing command mutex and freed before releasing it. Ordinary replies use 1024 bytes; library list/detail replies retain their original 4096-byte capacity. Low free heap or a failed allocation produces a `no-memory` response. The free-heap check does not measure heap fragmentation or guarantee allocation under all runtime conditions.

The rebuilt handler's compiler-reported frame is 1376 bytes. I did not enlarge any worker or app stack, and added no extra persistent buffer. The standalone FOBscan UI and radio-start behavior are unchanged; I cannot attribute the link overflow to that particular crash unless a dashboard link was active.

`tools/check_stack_usage.py` measures selected production functions with the SDK compiler using the generated compile database. I run it after `ufbt`; `STACK_USAGE.md` records the measured frames and the matching FAP hash. These checks do not prove whole-call-chain stack safety or replace physical device testing. I will not describe the reported FOBscan crash as resolved until I have tested the replacement on the device.

For installation, I replace the existing FAP with `FAP/fobworks_flipper.fap`, reboot, and first try entering/leaving FOBscan without dashboard links. Then I check receive-only operation with a dashboard link enabled. If the device faults again, I retain a photo of the full crash screen, including the thread name and any PC/LR/SP values, before dismissing it.

This package is based on the downloaded `main` source at `7846e1c`. I performed no commits, pushes, tags, workflow dispatches, or release uploads on GitHub. The published v1.4 asset is therefore still unchanged.

## Included changes

- Bridge: fail-closed empty defaults, ignored local configuration, authenticated inbound **and outbound** routing, reconnect reset and invalid-client bounds.
- FAP control link: mandatory random six-digit code whenever links start; show/rotate it locally. I reject empty, partial, overlong or non-digit replacement codes before modifying the current code. I serialize code generation against command dispatch.
- Plugin lifetime: I initialize before workers; hold a recursive guard across decoding and shared TE scratch; unloading waits for active borrowers. I clear submenu labels before unmapping their catalog.
- FOBfreq: microseconds, not fabricated ppm; minimum four samples, estimator resolution acknowledged, no transmitter/crystal identification claim.
- FOBtrack: explicit RKE/TPMS receive switching preserves observations; rolling RKE filter, bounded sliding window instead of silently stopping at record 64, reset action and structural/co-occurrence caveats.
- FOBreport: no inferred counter width, no receiver-window/security claim from locally predicted ranges.
- Distribution: SDK-built FAP synchronized in `FAP/` and `dist/`; fresh SHA256SUMS, BUILD_MANIFEST and size baseline. Future manual build workflow uploads checksums/manifest with its asset.
- Host fixtures and regression coverage for the production bridge sketch, plugin lifetime, code validation, timing inputs and rolling-log overflow. Bounded string-copy cleanups retain existing output behavior and permit strict optimizing GCC host builds.

I did not extend transmit, jamming, cryptographic recovery or rolling-code prediction behavior.

## Build and checks

Official SDK **1.4.3**, hardware Target **7**, API **87.1**, uFBT **0.2.6**.
I pinned the SDK archive digest in the workflow and build manifest.

From `source/fobworks_flipper`, I run `ufbt` with that SDK.
From the repository root:

```sh
make -C source/fobworks_flipper/tools test
python3 tools/sync_release.py --sync
python3 tools/sync_release.py --check
sha256sum -c SHA256SUMS
```

Host regressions use simulated signals: forced classification is not proof of successful live RF decoding. Runtime heap, stack, UI concurrency, SDK compatibility on other firmware branches and bridge electrical behavior need actual hardware.

Verified locally: the complete host suite passes with explicit optimizing GCC flags (`-O2 -Wall -Wextra -Werror`, C99 / C++17; C11 for the pthread fixture). Link: 67 checks; library contract: 51; TX ownership: 21; vehicle decoder: 25; keyvault: 14; lab modules: 29. Production bridge routing and unconfigured-startup fixtures pass; concurrent plugin-unload fixture passes. The SDK FAP and both plugins pass Target 7 / API 87.1 checks. All recorded section budgets, distribution consistency and checksum checks pass. Synthetic corpus: 120 forced labels match; Auto correct labels remain 10% because most of these protocols intentionally remain force-only.

## Suggested physical-device check

I use receive-only operation for these checks:

1. Install the bundled FAP on supported official firmware. Enter/leave the main menu and receive scenes repeatedly while a dashboard receives data; confirm plugin unload/reload remains stable and no heap failure occurs.
2. Enable Dashboard Link, read its six-digit code, verify a missing/wrong code fails, valid code succeeds, and blank/partial replacement fails without changing the existing code. Reconnect after local code rotation.
3. With an appropriately configured bridge, connect two sockets. Authenticate only one: confirm the other receives no Flipper telemetry or replies. Disconnect/reconnect and verify it is no longer authenticated.
4. Collect four timing observations per profile; confirm units are us and the result only describes coarse timing similarity.
5. Alternate FOBtrack modes on the same frequency. Confirm the log survives switching, continues after 64 observations, and OK clears it. Treat TPMS readings as structural candidates, not confirmed sensor/vehicle identity.

## My GitHub update

I copy this ZIP over a clean checkout of the matching base, inspect the diff, then commit/push myself. I check `LOCAL_CHANGES.md` and distribution metadata with the source. The GitHub release does not update just because I push: I upload the new FAP plus checksum/manifest files or manually run the existing Build FAP workflow. Its publish option remains an explicit manual choice. If I have newer edits than the stated base, I merge these changes rather than copying over my newer files.