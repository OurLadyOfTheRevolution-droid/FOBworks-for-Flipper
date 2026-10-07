# FOBworks — Flipper WiFi Devboard Bridge

This optional bridge uses the official **Flipper WiFi Devboard** (ESP32-S2), or
another ESP32 development board, to relay WebSocket traffic to the Flipper FAP
over UART. With the bridge and a compatible dashboard configured, the link can
be used without a USB tether.

```
 Dashboard (WiFi mode) ──ws://192.168.4.1:81──► ESP32 bridge ──UART 115200──► Flipper FAP
                        ◄──── JSON lines ───────             ◄──── JSON lines ────
```

The bridge only handles transport: it forwards complete newline-delimited JSON
lines in both directions and does not parse them. The wire schema is defined
by the Flipper FAP in `fobworks_flipper/link/flipper_link_proto.*`. USB
(Web Serial) and the WiFi bridge use the same protocol, so the dashboard does
not need separate command formats for the two connections.

## Flashing

1. Install the **arduinoWebSockets** library (Links2004) via the Arduino Library
   Manager. `WiFi.h` and `WebServer.h` ship with the ESP32 core.
2. Select your board (Flipper WiFi Devboard = *ESP32-S2 Dev Module*).
3. Set `FLIPPER_UART_RX` / `FLIPPER_UART_TX` for your wiring (see below).
4. Upload `fobworks_wifi_bridge.ino`.

## Wiring (bring-up seam)

The UART pins at the Flipper GPIO header vary by devboard revision. Connect the
ESP UART to the Flipper GPIO header as shown:

| Flipper GPIO | Signal        | ESP pin (default) |
|--------------|---------------|-------------------|
| pin 13 (TX)  | Flipper → ESP | `FLIPPER_UART_RX` = 18 |
| pin 14 (RX)  | ESP → Flipper | `FLIPPER_UART_TX` = 17 |
| pin 8/18     | GND           | GND               |

On the official S2 devboard, the header UART may instead connect to ESP UART0;
adjust the two pin definitions to match your wiring. The FAP's
`FlipperLinkUart` uses USART on pins **13/14 at 115200 8N1**.

## Using it

1. Run the FAP and enable **Dashboard link** in Advanced Settings. The UART
   remains disabled until you turn this option on.
2. Join WiFi network **`FOBworks-Flipper`** using the password you set in the
   sketch (`AP_PASS`). Set a unique password before use, keep the link private,
   and power the bridge off when idle.
3. Open the FOBworks dashboard over plain HTTP, choose **WiFi**, and connect to
   `ws://192.168.4.1:81`. Do not add a `/ws` path. Send
   `AUTH:<BRIDGE_KEY>` as the first WebSocket message; set both `AP_PASS` and
   `BRIDGE_KEY` in the sketch before flashing. Joining the AP alone is no longer
   enough to drive the radio.
4. Visit `http://192.168.4.1/` to view the bridge page and client count. This
   is not the FOBworks dashboard.

> **HTTPS note:** Browsers block `ws://` connections from an `https://` page as
> mixed content. When using the WiFi bridge, serve or open the dashboard over
> `http://`, or run it from `localhost`.

## Scope

The bridge supports one CC1101 radio at a time. This hardware does not support
simultaneous multi-radio jamming and capture, multi-channel sweeps, the
Toyota-band SX1278, or CAN/BLE/UWB. Commands for those features return
`unsupported`.

— OurLadyOfTheRevolution-droid
