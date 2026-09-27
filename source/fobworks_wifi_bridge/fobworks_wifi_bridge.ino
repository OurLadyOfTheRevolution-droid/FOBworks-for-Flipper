/*
 * FOBworks — Flipper WiFi Devboard bridge
 *
 * Relays newline-delimited JSON between the dashboard and the Flipper FAP.
 * The dashboard opens ws://192.168.4.1:81 with no path. This sketch does not
 * parse the lines. The FAP speaks that protocol on USART pins 13 and 14,
 * 115200 8N1, after Dashboard link is turned on in Advanced Settings.
 *
 * Target: ESP32-S2 (official Flipper WiFi Devboard) or ESP32. Use Serial1 so
 * the USB console stays free. Flipper TX (pin 13) lands on ESP RX, Flipper RX
 * (pin 14) lands on ESP TX.
 *
 * Library: arduinoWebSockets (Links2004). WiFi and WebServer ship with the
 * ESP32 core.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

/* Anyone on this AP can send the FAP's radio commands. The dashboard sends
 * no WebSocket token, so the AP password is the access control. Change it
 * before you flash. */
static const char* AP_SSID = "FOBworks-Flipper";
static const char* AP_PASS = "CHANGE-ME-unique-strong-pass";

#define FLIPPER_UART    Serial1
#define FLIPPER_BAUD    115200
#define FLIPPER_UART_RX 18   /* ESP RX <- Flipper TX, pin 13 */
#define FLIPPER_UART_TX 17   /* ESP TX -> Flipper RX, pin 14 */
#define LINE_MAX        512  /* FAP transmit buffer size */

WebServer http(80);
WebSocketsServer ws(81);

static char lineBuf[LINE_MAX];
static size_t lineLen = 0;

static const char* LANDING =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>FOBworks Flipper Bridge</title>"
    "<body style='font-family:system-ui;background:#0b0f14;color:#e6edf3;"
    "max-width:34rem;margin:3rem auto;padding:0 1rem;line-height:1.5'>"
    "<h1>FOBworks Flipper WiFi Bridge</h1>"
    "<p>Join <b>FOBworks-Flipper</b>, open the FOBworks dashboard over plain "
    "http, choose <b>WiFi</b>, and connect to "
    "<code>ws://192.168.4.1:81</code>.</p>"
    "<p>On the Flipper, turn <b>Dashboard link</b> on in Advanced Settings "
    "before connecting.</p>"
    "<p id=st>WebSocket clients: 0</p>"
    "<script>setInterval(async()=>{try{const r=await fetch('/status');"
    "document.getElementById('st').textContent="
    "'WebSocket clients: '+(await r.json()).clients;}catch(e){}},2000)</script>";

static void pumpFlipperToWs() {
    while(FLIPPER_UART.available()) {
        char c = (char)FLIPPER_UART.read();
        if(c == '\n' || c == '\r') {
            if(lineLen > 0) {
                lineBuf[lineLen] = '\0';
                ws.broadcastTXT(lineBuf, lineLen);
                lineLen = 0;
            }
        } else if(lineLen < LINE_MAX - 1) {
            lineBuf[lineLen++] = c;
        } else {
            lineLen = 0;
        }
    }
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
    (void)num;
    if(type != WStype_TEXT || len == 0 || len >= LINE_MAX) return;
    FLIPPER_UART.write(payload, len);
    if(payload[len - 1] != '\n' && payload[len - 1] != '\r')
        FLIPPER_UART.write('\n');
}

void setup() {
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
    http.handleClient();
    ws.loop();
    pumpFlipperToWs();
}
