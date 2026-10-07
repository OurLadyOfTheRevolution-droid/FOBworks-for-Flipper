/*
 * FOBworks — Flipper WiFi Devboard bridge
 *
 * Forwards newline-delimited JSON between the dashboard and Flipper app.
 * The dashboard connects to ws://192.168.4.1:81 (no path). The bridge does
 * not parse messages; it passes them to the FAP over USART pins 13 and 14 at
 * 115200 8N1. Turn on Dashboard link in Advanced Settings first.
 *
 * Target: ESP32-S2 (official Flipper WiFi Devboard) or ESP32. Serial1 keeps
 * the USB console available. Connect Flipper TX (pin 13) to ESP RX and
 * Flipper RX (pin 14) to ESP TX.
 *
 * Uses arduinoWebSockets (Links2004); WiFi and WebServer come with the ESP32
 * core.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "bridge_policy.h"

/* Configure locally; never commit deployment credentials. */
#if __has_include("bridge_config.h")
#include "bridge_config.h"
#endif
#ifndef FOBWORKS_AP_PASSWORD
#define FOBWORKS_AP_PASSWORD ""
#endif
#ifndef FOBWORKS_BRIDGE_KEY
#define FOBWORKS_BRIDGE_KEY ""
#endif

/* Anyone connected to this AP can send radio commands. The dashboard does not
 * authenticate WebSocket clients, so the AP password is the only access
 * control. Replace the default before flashing. */
static const char* AP_SSID = "FOBworks-Flipper";
static const char* AP_PASS = FOBWORKS_AP_PASSWORD;

/* Bridge-level shared secret. A WebSocket client must send
 *   AUTH:<key>\n
 * before any other frame is forwarded to the Flipper. This is a second,
 * independent gate on top of the AP password, so joining the AP is not enough
 * to drive the radio. Replace before flashing. */
static const char* BRIDGE_KEY = FOBWORKS_BRIDGE_KEY;

#define FLIPPER_UART    Serial1
#define FLIPPER_BAUD    115200
#define FLIPPER_UART_RX 18   /* ESP RX <- Flipper TX, pin 13 */
#define FLIPPER_UART_TX 17   /* ESP TX -> Flipper RX, pin 14 */
#undef LINE_MAX /* macOS host toolchains predefine it via syslimits.h */
#define LINE_MAX        512  /* FAP transmit buffer size */

WebServer http(80);
WebSocketsServer ws(81);

static char lineBuf[LINE_MAX];
static size_t lineLen = 0;
static bool discardLine = false;

/* Per-client auth state: client i may forward only after authenticating. */
static bool clientAuthed[WEBSOCKETS_SERVER_CLIENT_MAX] = {false};
static bool configured = false;

static const char* LANDING =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>FOBworks Flipper Bridge</title>"
    "<body style='font-family:system-ui;background:#0b0f14;color:#e6edf3;"
    "max-width:34rem;margin:3rem auto;padding:0 1rem;line-height:1.5'>"
    "<h1>FOBworks Flipper WiFi Bridge</h1>"
    "<p>Join <b>FOBworks-Flipper</b>, open the FOBworks dashboard over plain "
    "HTTP, choose <b>WiFi</b>, then connect to "
    "<code>ws://192.168.4.1:81</code>.</p>"
    "<p>Send <code>AUTH:&lt;bridge-key&gt;</code> as your first WebSocket "
    "message to enable command forwarding.</p>"
    "<p>On the Flipper, enable <b>Dashboard link</b> in Advanced Settings "
    "before you connect.</p>"
    "<p id=st>WebSocket clients: 0</p>"
    "<script>setInterval(async()=>{try{const r=await fetch('/status');"
    "document.getElementById('st').textContent="
    "'WebSocket clients: '+(await r.json()).clients;}catch(e){}},2000)</script>";

static void pumpFlipperToWs() {
    while(FLIPPER_UART.available()) {
        char c = (char)FLIPPER_UART.read();
        if(c == '\n' || c == '\r') {
            if(discardLine) {
                discardLine = false;
                lineLen = 0;
                continue;
            }
            if(lineLen > 0) {
                lineBuf[lineLen] = '\0';
                for(size_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
                    if(bridge_client_authorized(
                           clientAuthed, WEBSOCKETS_SERVER_CLIENT_MAX, i))
                        ws.sendTXT((uint8_t)i, lineBuf, lineLen);
                }
                lineLen = 0;
            }
        } else if(discardLine) {
            continue;
        } else if(lineLen < LINE_MAX - 1) {
            lineBuf[lineLen++] = c;
        } else {
            lineLen = 0;
            discardLine = true;
        }
    }
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
    (void)num;
    if(type == WStype_DISCONNECTED) {
        if(num < WEBSOCKETS_SERVER_CLIENT_MAX) clientAuthed[num] = false;
        return;
    }
    if(type == WStype_CONNECTED) {
        if(num < WEBSOCKETS_SERVER_CLIENT_MAX) clientAuthed[num] = false;
        return;
    }
    if(type != WStype_TEXT || len == 0 || len >= LINE_MAX) return;

    /* Handshake gate: an unauthenticated client may only send AUTH:<key>. */
    if(num < WEBSOCKETS_SERVER_CLIENT_MAX && !clientAuthed[num]) {
        static const char AUTH_PFX[] = "AUTH:";
        if(len == strlen(AUTH_PFX) + strlen(BRIDGE_KEY) &&
           memcmp(payload, AUTH_PFX, strlen(AUTH_PFX)) == 0 &&
           memcmp(payload + strlen(AUTH_PFX), BRIDGE_KEY, strlen(BRIDGE_KEY)) == 0) {
            clientAuthed[num] = true;
            ws.sendTXT(num, "{\"event\":\"bridge_auth\",\"ok\":true}\n");
        } else {
            ws.sendTXT(num, "{\"event\":\"bridge_auth\",\"ok\":false}\n");
        }
        return;
    }

    if(num >= WEBSOCKETS_SERVER_CLIENT_MAX || !clientAuthed[num]) {
        ws.sendTXT(num, "{\"event\":\"bridge_auth\",\"ok\":false}\n");
        return;
    }

    FLIPPER_UART.write(payload, len);
    if(payload[len - 1] != '\n' && payload[len - 1] != '\r')
        FLIPPER_UART.write('\n');
}

void setup() {
    if(!bridge_config_valid(FOBWORKS_AP_PASSWORD, BRIDGE_KEY)) {
        /* Missing/placeholder credentials must not expose an AP or server. */
        return;
    }
    configured = true;
    FLIPPER_UART.begin(FLIPPER_BAUD, SERIAL_8N1, FLIPPER_UART_RX, FLIPPER_UART_TX);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    http.on("/", []() { http.send(200, "text/html", LANDING); });
    http.on("/status", []() {
        char j[48];
        snprintf(j, sizeof(j), "{\"clients\":%u}", ws.connectedClients());
        http.send(200, "application/json", j);
    });
    http.begin();

    ws.begin();
    ws.onEvent(onWsEvent);
}

void loop() {
    if(!configured) return;
    http.handleClient();
    ws.loop();
    pumpFlipperToWs();
}
