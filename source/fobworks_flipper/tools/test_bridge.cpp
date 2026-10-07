/* These public fixtures are host-test inputs, never deployment credentials. */
#ifndef TEST_UNCONFIGURED
#define FOBWORKS_AP_PASSWORD "host-test-password-only"
#define FOBWORKS_BRIDGE_KEY "host-test-bridge-key-only"
#endif
#include "../../fobworks_wifi_bridge/fobworks_wifi_bridge.ino"
#include <cassert>
#include <iostream>

#ifndef TEST_UNCONFIGURED
static void event(uint8_t client, const char* text) {
    onWsEvent(client, WStype_TEXT, (uint8_t*)text, strlen(text));
}
#endif

int main() {
    setup();
#ifdef TEST_UNCONFIGURED
    assert(!configured && !WiFi.started);
    loop();
#else
    assert(configured && WiFi.started);
    assert(!bridge_config_valid("CHANGE-ME-password", "CHANGE-ME-bridge-key"));
    assert(!bridge_config_valid("short", "a-sufficient-length-key"));
    onWsEvent(0, WStype_CONNECTED, nullptr, 0);
    onWsEvent(1, WStype_CONNECTED, nullptr, 0);
    event(0, "{\"cmd\":\"status\"}");
    assert(Serial1.forwarded.empty());
    event(1, "AUTH:wrong");
    assert(!clientAuthed[1]);
    event(0, "AUTH:host-test-bridge-key-only");
    assert(clientAuthed[0]);
    ws.sent.clear();
    for(char c : std::string("{\"event\":\"signal\"}\n")) Serial1.incoming.push_back(c);
    pumpFlipperToWs();
    assert(ws.sent.size() == 1 && ws.sent[0].client == 0);
    ws.sent.clear();
    std::string oversized(LINE_MAX + 4, 'x');
    oversized += "{\"event\":\"fake-suffix\"}\n";
    for(char c : oversized) Serial1.incoming.push_back(c);
    pumpFlipperToWs();
    assert(ws.sent.empty());
    for(char c : std::string("{\"event\":\"recovered\"}\n")) Serial1.incoming.push_back(c);
    pumpFlipperToWs();
    assert(ws.sent.size() == 1 && ws.sent[0].body == "{\"event\":\"recovered\"}");
    event(0, "{\"cmd\":\"status\"}");
    assert(Serial1.forwarded == "{\"cmd\":\"status\"}\n");
    onWsEvent(0, WStype_DISCONNECTED, nullptr, 0);
    assert(!clientAuthed[0]);
    ws.sent.clear();
    for(char c : std::string("{\"event\":\"signal\"}\n")) Serial1.incoming.push_back(c);
    pumpFlipperToWs();
    assert(ws.sent.empty());
    onWsEvent(0, WStype_CONNECTED, nullptr, 0);
    assert(!clientAuthed[0]);
    event(WEBSOCKETS_SERVER_CLIENT_MAX, "AUTH:host-test-bridge-key-only");
    assert(!bridge_client_authorized(clientAuthed, WEBSOCKETS_SERVER_CLIENT_MAX,
                                     WEBSOCKETS_SERVER_CLIENT_MAX));
#endif
    std::cout << "Production bridge routing/auth checks passed\n";
}
