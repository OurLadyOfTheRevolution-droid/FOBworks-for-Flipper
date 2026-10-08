# App-thread periodic timer candidate — official 1.4.3

## Evidence

The previous live-display candidate still crashed after roughly two seconds. During USB CLI logging, I observed no immediate crash, but OK and Back stopped responding during a range sweep. Disconnecting USB released navigation; entering FOBscan again then caused the usual two-second crash.

In a later run of the same installed candidate, I scanned at 330 MHz without a crash, then exited, selected 315 MHz, and returned to FOBscan; it crashed after about ten seconds. This weakens a fixed startup deadline hypothesis. RF activity, the decoder path and heap layout remain possible factors; frequency alone is not established as the cause.

My USB log confirms official firmware 1.4.3 and normal FAP loading. It records an additional 11,244-byte ELF allocation roughly two seconds after the main FAP loaded, consistent with a lazily loaded app plugin. It contains no crash dump, failing thread, or register values. It does not prove which instruction faults. A preload error for `tester.fap` is separate from the successful load of `fobworks_flipper.fap`.

USB logging changed the observed behavior; that run is not a stability pass.

## Corrected execution-context defect

The previous `FuriTimer` callback ran on official 1.4.3's shared TimersSrv. The official FreeRTOS configuration gives it 256 stack words = 1,024 bytes. The callback alone used a measured 424-byte ARM frame, before SDK, formatting, radio, or decoder callees. It formatted a floating-point heartbeat even with no dashboard links, and could decode captures and load plugins during headless scanning. It also posted into a bounded dispatcher queue with an indefinite wait. That is unsuitable work for a shared timer-service callback.

The candidate uses the SDK's periodic `FuriEventLoopTimer`, owned by the app's existing 4,096-byte thread. It dispatches the scene status event directly, without waiting on its own custom-event queue. With no allocated dashboard links it returns after scene processing, skipping heartbeat/remote formatting. The SDK's inactivity tick was deliberately not used: busy receive events could otherwise postpone sweep/heartbeat updates.

I did not enlarge the thread stack. Timer allocation/start/stop/free remains on the event-loop owner thread, and the timer is freed before dispatcher teardown. The prior bounded, locking live-display snapshot is preserved.

## Checks

- Official 1.4.3 / Target 7 / API 87.1 SDK build.
- ARM frame regression checks, now including the periodic callback.
- Production-source lifecycle/thread-context guard.
- Native protocol/display regression tests and release/checksum validation.

The new periodic callback's measured frame remains 424 bytes; it now belongs to the app thread rather than TimersSrv. GUI draw remains 40 bytes. No decoder registry/frequency policy was changed: inspection showed Toyota is already force-only, and automatic decoding can lazily load the extra protocol plugin on any supported receive frequency.

These checks do not establish physical-device crash resolution or reliable OK/Back behavior. App call-chain headroom and USB-log interference still need physical confirmation. I added no additional diagnostic logging.

## Install

I replace `apps/Sub-GHz/fobworks_flipper.fap` with the installer ZIP's `fobworks_flipper.fap`, keeping that exact name. I reboot.

I first test normal FOBscan WITHOUT running the USB logging helper. If it stays open past the previous two-second failure, I leave it for 30 seconds, then start a range sweep and check OK stops it and Back returns to the menu. If it still crashes, I report that result rather than counting a USB-logged run as a pass. I do not delete settings, keys, assets, or library files.

I have kept the complete matching source with this build. These are my local changes; I have not committed or pushed them to GitHub.