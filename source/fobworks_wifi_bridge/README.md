# Optional ESP32-S2 WiFi Devboard bridge

GPIO UART links the official WiFi Devboard to the Flipper. AP clients use
WebSocket port 81; the HTTP endpoint serves a status page only. This sketch
does not implement TLS. Use it only on a trusted local network.

## Configure before flashing

Create `bridge_config.h` beside the sketch, locally. It is gitignored.
Define `FOBWORKS_AP_PASSWORD` (8–63 characters) and `FOBWORKS_BRIDGE_KEY`
(16–64 characters) to **unique random credentials**. Do not commit that file.
No deployment credentials or usable defaults are included.
Without valid configuration, the sketch starts neither AP nor servers.
The AP SSID is `FOBworks`.

Wiring: Flipper GPIO UART TX → Devboard RX, RX → TX, common ground.
Use the UART pins and baud rate defined near the top of the sketch; verify
these match the Flipper build before wiring. Never connect incompatible
voltages or power sources.

## Two authentication layers

1. Associate with the AP using its configured password.
2. Send the text frame `AUTH:<your bridge key>` over WebSocket.
   Only authenticated sockets may forward commands **or receive UART data**.
   A reconnect requires authentication again.
3. Enable **Dashboard Link** on the Flipper. **LinkAuth** shows its randomly
   generated six-digit code; select the other value to generate a new one.
   Include that code in Flipper command JSON as `"code":"......"`.
   The bridge key does not bypass this independent Flipper gate.

The remote `auth` command may replace the Flipper code with exactly six digits,
but cannot clear it. HTTP does not expose Flipper serial responses.
Do not paste credentials into screenshots, issue reports, or source control.

## Verification

`make -C source/fobworks_flipper/tools test` exercises the production sketch
with host substitutes for WiFi/UART, including:

- unconfigured startup fails closed;
- unauthenticated clients cannot send commands;
- only authenticated clients receive UART responses;
- failed authentication and reconnection do not grant access;
- invalid client indexes are ignored.

These are routing/authentication regressions, not hardware integration tests.
Confirm actual ESP32-S2 compilation, UART wiring, and reconnect behavior on
your boards before deployment.
