# FOBworks — Flipper WiFi Devboard Bridge

Turns the official **Flipper WiFi Devboard** (ESP32-S2) — or any ESP32 dev
module — into a WebSocket-to-UART relay so the FOBworks dashboard can
drive the Flipper Zero build of FOBscan/FOBclone/FOBcatch/FOBback **without a
USB tether**.

```
 Dashboard (WiFi mode) ──ws://192.168.4.1:81──► ESP32 bridge ──UART 115200──► Flipper FAP
                        ◄──── JSON lines ───────             ◄──── JSON lines ────
```

The bridge is deliberately **transport-dumb**: it forwards whole
newline-delimited JSON lines in both directions and never parses them. The wire
schema lives entirely in the Flipper FAP
(`fobworks_flipper/link/flipper_link_proto.*`). USB (Web Serial) and this
WiFi path speak the identical protocol, so the dashboard needs no per-transport
logic.

## Flashing

1. Install the **arduinoWebSockets** library (Links2004) via the Arduino Library
   Manager. `WiFi.h` and `WebServer.h` ship with the ESP32 core.
2. Select your board (Flipper WiFi Devboard = *ESP32-S2 Dev Module*).
3. Set `FLIPPER_UART_RX` / `FLIPPER_UART_TX` for your wiring (see below).
4. Upload `fobworks_wifi_bridge.ino`.

## Wiring (bring-up seam)

The UART pins facing the Flipper's GPIO header differ by devboard revision.
Wire the ESP UART to the Flipper's GPIO header:

| Flipper GPIO | Signal        | ESP pin (default) |
|--------------|---------------|-------------------|
| pin 13 (TX)  | Flipper → ESP | `FLIPPER_UART_RX` = 18 |
| pin 14 (RX)  | ESP → Flipper | `FLIPPER_UART_TX` = 17 |
| pin 8/18     | GND           | GND               |

On the official S2 devboard the header UART may be wired to the ESP's UART0
instead — adjust the two pin defines accordingly. The Flipper FAP's UART link
(`FlipperLinkUart`) uses USART on pins **13/14 at 115200 8N1**.

## Using it

1. Run the FAP and turn **Dashboard link** on in Advanced Settings. The UART
   stays down until that switch is on.
2. Join WiFi network **`FOBworks-Flipper`** using the password you set in the
   sketch (`AP_PASS`). **Security:** the AP password is the only access control.
   Anyone on the AP can send the FAP's link commands. Set a unique password
   before use, keep the link private, and power the bridge off when idle.
3. Open the FOBworks dashboard over plain http, pick **WiFi**, and connect to
   `ws://192.168.4.1:81` (no `/ws` path).
4. Visit `http://192.168.4.1/` for the bridge page and client count. That page
   is not the dashboard.

> **HTTPS note:** browsers block `ws://` from an `https://` page (mixed
> content). Serve/open the dashboard over `http://` when using the WiFi bridge,
> or run it from `localhost`.

## Scope

One CC1101, one radio at a time. Multi-radio jam plus capture, multi-channel
sweep, Toyota-band (SX1278), and CAN/BLE/UWB are outside this hardware. Those
commands get an `unsupported` reply.
