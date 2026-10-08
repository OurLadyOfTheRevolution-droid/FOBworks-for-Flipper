# FOBscan live-display replacement candidate

## Evidence and limitations

The device is running official firmware 1.4.3. The original replacement still crashed after approximately two seconds. Both display-only and RX-only probes passed. Crucially, the diagnostic **Normal FOBscan** control also crashed after two seconds with autosave disabled. Autosave is therefore not a necessary trigger for this observed crash.

This narrows the failure to the combined live display/receive operation. It does not identify the exact faulting instruction or prove stack overflow. This build addresses an observed software defect in that path: the GUI was reading mutable app/capture state without synchronization.

## Changes

- FOBscan now has an SDK `ViewModelTypeLocking` model containing owned, preformatted text. The SDK holds its mutex throughout drawing; I hold the same mutex during publication.
- The GUI no longer dereferences `FlipperApp`, live decode results or settings, or calls `snprintf`. Formatting occurs on the app thread.
- Nonfinite/out-of-range numeric values show `--`. Decoder labels are bounded by their source arrays even when their terminators are missing.
- Initial text is published before showing the view. Live ticks and all tuning/range/sweep changes continue publishing updated text.
- No device key or private-key string is copied into the display model.
- Stack sizes, RF capture, transmit utilities, autosave settings and user data are unchanged in the production build.

ARM frame measurements: GUI draw **40 bytes**, down from **208**; publication 64 bytes; snapshot builder 96 bytes. These are individual frames, not runtime whole-thread measurements.

## RAM budget

The snapshot payload is 277 bytes. The previous model held one pointer. The locking model also has a wrapper, SDK recursive mutex and allocator overhead. The release checker now reserves 512 bytes for that display allocation.

The code section grew by roughly 454 bytes. To accommodate it without raising the previous combined RAM ceiling, I transferred 512 bytes of unused readonly-data allowance to code, and reserved another 512 bytes for the model. The checker enforces sections **plus model reserve <= 84,921 bytes**. This static budget is not proof of physical-device heap sufficiency.

## Install

The installer ZIP contains the production candidate as **fobworks_flipper.fap**. I replace the normal FAP in `apps/Sub-GHz`, preserving that exact filename. I reboot and open normal FOBscan. It should show live RSSI/capture updates; I check whether it stays open beyond the previous two-second failure.

I do not delete settings, keys, library files or asset folders. A separate source ZIP contains the complete source and verification reports. I committed or pushed no changes to GitHub.

The physical-device outcome remains unconfirmed until I run this candidate.