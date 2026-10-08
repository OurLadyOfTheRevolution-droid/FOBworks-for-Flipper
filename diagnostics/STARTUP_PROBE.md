# FOBscan startup isolation probe — official firmware 1.4.3

This is a diagnostic build, **not a crash fix**. The replacement production FAP still produced my MPU fault. Its exact failing thread/instruction is not yet known, and the previously corrected dashboard-worker overflow does not establish the cause of this standalone failure.

The stored probe has now been rebuilt with the live-display snapshot and app-owned periodic timer changes; it is not the identical binary used for the earlier device results. Those earlier results were display-only PASS, RX-only PASS, and normal FOBscan FAIL after about two seconds, even with autosave disabled. For the new production candidate and its installer, see `APP_TIMER_CANDIDATE.md`. The two-folder installation instructions below describe the earlier diagnostic installer.

## Install and test

1. I first install the installer ZIP's `normal/fobworks_flipper.fap` over the existing normal FAP in the SD card's `apps/Sub-GHz` folder. I keep that exact filename, reboot, and check whether normal FOBscan still crashes. The earlier suffixed standalone download was a packaging mistake: official firmware extracts embedded assets using the FAP's filename stem, whereas the app's compiled asset paths use `fobworks_flipper`.
2. If it still crashes, I copy the ZIP's `probe/fobworks_flipper.fap` into a separate SD folder, `apps/Debug`, keeping the same filename. It can coexist with the normal FAP. I do not delete settings, library, keys, or other user data. I reboot, then launch `apps/Debug/fobworks_flipper.fap` through the Flipper file browser. The first menu must say **FOBscan startup probe**. If it shows the normal full utilities menu, I launched the wrong FAP.
3. I choose **1. Display only (no RX)**. I wait about five seconds. This uses the existing FOBscan drawing/model but starts no radio reception. I note whether it displays normally, crashes immediately, or crashes later/on Back.
4. I return with Back, or reboot if it crashed. I choose **2. RX only (standard UI)**. I wait about five seconds. This runs the same RF receive and decoder/event path but displays an ordinary SDK menu instead of invoking the custom FOBscan drawing/input callbacks. I note whether it crashes immediately or after reception begins.
5. The optional **3. Normal FOBscan** combines the current display and receive paths as a control. Its crash has already been reported; I do not repeat it unnecessarily.

The probe exposes no transmit or dashboard-link settings in its menu. FOBscan tuning/input actions, automatic capture saving, and settings writes on scene exit are disabled in this build. I do not delete or migrate the settings/library files. Production builds retain their original behavior.

Both probes failing, both passing, a Back-only failure, or a failure only after an RF signal are all useful results. Neither probe result alone proves stack overflow: a bad pointer, lifecycle error, or SDK/HAL problem is still possible. The standard UI also changes rendering/heap pressure, so these are isolation tests, not byte-identical reproductions.

## Local source/build

The probe additions are guarded by `FOBSCAN_STARTUP_PROBE`; without that define they are absent from the production app.

From `source/fobworks_flipper`, with the pinned official 1.4.3 SDK:

```sh
ufbt --extra-define FOBSCAN_STARTUP_PROBE=1
```

After that build, from the repository root:

```sh
python3 tools/check_stack_usage.py --report diagnostics/STARTUP_PROBE_STACK_USAGE.md
```

The repository's diagnostic snapshot is stored as `diagnostics/fobworks_flipper_startup_probe.fap` to distinguish it from the production build. When installing it, I always use the filename `fobworks_flipper.fap` in the separate Debug folder.

Then I run `ufbt` without the define to restore the normal distribution build, followed by the usual stack and release checks.

I performed no commits, pushes, GitHub Actions or remote release changes.