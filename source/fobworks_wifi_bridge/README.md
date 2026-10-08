# Optional ESP32-S2 WiFi Devboard bridge

GPIO UART links the official WiFi Devboard to the Flipper. AP clients use
WebSocket port 81; the HTTP endpoint serves a status page only. This sketch
does not implement TLS. I use it only on a trusted local network.

## Configure before flashing

I create `bridge_config.h` beside the sketch, locally. It is gitignored.
I define `FOBWORKS_AP_PASSWORD` (8–63 characters) and `FOBWORKS_BRIDGE_KEY`
(16–64 characters) to **unique random credentials**. I do not commit that file.
No deployment credentials or usable defaults are included.
Without valid configuration, the sketch starts neither AP nor servers.
The AP SSID is `FOBworks-Flipper`.

Wiring: Flipper GPIO UART TX → Devboard RX, RX → TX, common ground.
I use the UART pins and baud rate defined near the top of the sketch; I verify
these match the Flipper build before wiring. I never connect incompatible
voltages or power sources.

## Two authentication layers

1. I associate with the AP using its configured password.
2. I send the text frame `AUTH:<BRIDGE_KEY>` over WebSocket.
   Only authenticated sockets may forward commands **or receive UART data**.
   A reconnect requires authentication again.
3. I enable **Dashboard Link** on the Flipper. **LinkAuth** shows its randomly
   generated six-digit code; I select the other value to generate a new one.
   I include that code in Flipper command JSON as "code":"......".
   The bridge key does not bypass this independent Flipper gate.

The remote `auth` command may replace the Flipper code with exactly six digits,
but cannot clear it. HTTP does not expose Flipper serial responses.
I do not paste credentials into screenshots, issue reports, or source control.

## Verification

`make -C source/fobworks_flipper/tools test` exercises the production sketch
with host substitutes for WiFi/UART, including:

- unconfigured startup fails closed;
- unauthenticated clients cannot send commands;
- only authenticated clients receive UART responses;
- failed authentication and reconnection do not grant access;
- invalid client indexes are ignored.

These are routing/authentication regressions, not hardware integration tests.
I confirm actual ESP32-S2 compilation, UART wiring, and reconnect behavior on
my boards before deployment.