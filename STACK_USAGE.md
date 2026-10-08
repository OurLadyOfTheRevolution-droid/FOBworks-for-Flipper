# ARM stack-frame checks

Compiler: the official 1.4.3 SDK compiler and production compile commands.
FAP SHA-256: `039d75e9d37c83237d295b7503a9fe43e28b6448650196dd9be7e8803ae96ce5`

| Function | Measured frame (bytes) | Frame budget (bytes) |
| --- | ---: | ---: |
| `flipper_tick_cb` | 424 | 512 |
| `flipper_app_handle_command` | 1376 | 1536 |
| `feed_byte` | 136 | 512 |
| `usb_rx_thread` | 96 | 128 |
| `uart_rx_thread` | 32 | 128 |
| `flipper_capture_notify_worker` | 40 | 256 |
| `flipper_capture_start` | 24 | 256 |
| `flipper_scene_fobscan_on_enter` | 16 | 256 |
| `flipper_fobscan_draw_cb` | 40 | 64 |
| `fobscan_redraw` | 64 | 192 |
| `flipper_fobscan_display_build` | 96 | 256 |
| `fobscan_consume_flush` | 432 | 512 |

I measured individual compiler-reported frames, not total thread usage.
Link workers and the app have 4096-byte stacks; the capture-notify worker
and official GUI service have separate 2048-byte stacks.
flipper_tick_cb runs on the app-owned event loop, not the firmware's
1024-byte TimersSrv. Its scene/decoder/format callees still add frames.
I still need to check SDK callees, plugin loading, interrupt/context saves
and runtime high-water marks on the device.

I did not capture the MPU fault's thread or PC. I verified the corrected
command-handler overflow, but I have not established the exact cause
of my FOBscan crash.
