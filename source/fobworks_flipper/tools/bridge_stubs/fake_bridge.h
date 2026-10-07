#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#define WIFI_AP 1
#define SERIAL_8N1 1
#define WEBSOCKETS_SERVER_CLIENT_MAX 4
enum WStype_t { WStype_CONNECTED, WStype_DISCONNECTED, WStype_TEXT };

struct FakeSerial {
    std::deque<char> incoming;
    std::string forwarded;
    void begin(int, int, int, int) {}
    int available() { return (int)incoming.size(); }
    char read() { char c = incoming.front(); incoming.pop_front(); return c; }
    void write(const uint8_t* p, size_t n) { forwarded.append((const char*)p, n); }
    void write(char c) { forwarded += c; }
} Serial1;

struct FakeWifi {
    bool started = false;
    void mode(int) {}
    void softAP(const char*, const char*) { started = true; }
} WiFi;

struct WebServer {
    explicit WebServer(int) {}
    template<typename T> void on(const char*, T) {}
    void send(int, const char*, const char*) {}
    void begin() {}
    void handleClient() {}
};

struct WebSocketsServer {
    struct Message { uint8_t client; std::string body; };
    std::vector<Message> sent;
    explicit WebSocketsServer(int) {}
    void sendTXT(uint8_t id, const char* p, size_t n) {
        sent.push_back({id, std::string(p, n)});
    }
    void sendTXT(uint8_t id, const char* p) { sendTXT(id, p, strlen(p)); }
    unsigned connectedClients() { return WEBSOCKETS_SERVER_CLIENT_MAX; }
    void begin() {}
    template<typename T> void onEvent(T) {}
    void loop() {}
};
