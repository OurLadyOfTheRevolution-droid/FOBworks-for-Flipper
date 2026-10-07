# ARM stack-frame checks

Compiler: the official 1.4.3 SDK compiler and production compile commands.
FAP SHA-256: `43acc32ecd7af39ed6227a442968ea7971b17376e6d2c972f87b70409450c0ba`

| Function | Measured frame (bytes) | Frame budget (bytes) |
| --- | ---: | ---: |
| `flipper_app_handle_command` | 1376 | 1536 |
| `feed_byte` | 136 | 512 |
| `usb_rx_thread` | 96 | 128 |
| `uart_rx_thread` | 32 | 128 |
| `flipper_capture_notify_worker` | 40 | 256 |
| `flipper_capture_start` | 24 | 256 |
| `flipper_scene_fobscan_on_enter` | 16 | 256 |
| `flipper_fobscan_draw_cb` | 208 | 320 |
| `fobscan_consume_flush` | 432 | 512 |

These are individual compiler-reported frames, not total thread usage.
Link workers and the app have 4096-byte stacks; the capture-notify worker
and official GUI service have separate 2048-byte stacks.
SDK callees, plugin loading, interrupt/context saves and runtime high-water
marks still need physical-device qualification.

The reported MPU fault's thread and PC were not captured. The corrected
command-handler overflow is verified, but attribution of that
FOBscan-opening crash remains unconfirmed.
