#include "../flipper_fobscan_app.h"

/* Launch image only. The JSON dashboard (flipper_link.c / _proto.c /
   _dispatch.c) stays in the tree for the host tests, but it is not linked
   into the FAP: .text alone was larger than the contiguous RAM block
   firmware 1.3.3 can give an external app. On-device USB control is off
   until that block fits again. Radio ownership for the GUI stays here. */

FlipperLink* flipper_link_alloc(void* app_ctx, FlipperLinkTransport transport) {
    (void)app_ctx;
    (void)transport;
    return NULL;
}

void flipper_link_free(FlipperLink* link) { (void)link; }

void flipper_link_send_line(FlipperLink* link, const char* data, size_t len) {
    (void)link;
    (void)data;
    (void)len;
}

FlipperLinkTransport flipper_link_transport(const FlipperLink* link) {
    (void)link;
    return FlipperLinkUsb;
}

uint32_t flipper_link_session_id(const FlipperLink* link) {
    (void)link;
    return 0;
}

void flipper_app_broadcast(FlipperApp* app, const char* line, size_t len) {
    (void)app;
    (void)line;
    (void)len;
}

void flipper_app_handle_command(void* app_ctx, const FlipperCmd* cmd, FlipperLink* origin) {
    (void)app_ctx;
    (void)cmd;
    (void)origin;
}

void flipper_app_link_disconnected(void* app_ctx, FlipperLink* link) {
    (void)app_ctx;
    (void)link;
}

size_t flipper_proto_emit_heartbeat(
    char* out, size_t n, float freq_mhz, bool scanning, int batt_pct,
    uint32_t uptime_s, bool cc1101_ok, int keys_used) {
    (void)out; (void)n; (void)freq_mhz; (void)scanning; (void)batt_pct;
    (void)uptime_s; (void)cc1101_ok; (void)keys_used;
    return 0;
}

size_t flipper_proto_emit_signal(
    char* out, size_t n, const FlipperDecodeResult* r, float rssi_dbm) {
    (void)out; (void)n; (void)r; (void)rssi_dbm;
    return 0;
}

void flipper_app_gui_radio_acquire(FlipperApp* app) {
    if(!flipper_radio_ensure(app)) return;
    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    if(app->remote_scanning) app->remote_scanning = false;
    app->gui_radio_active = true;
    furi_mutex_release(app->radio_mutex);
}

void flipper_app_gui_radio_release(FlipperApp* app) {
    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    app->gui_radio_active = false;
    furi_mutex_release(app->radio_mutex);
}

void flipper_app_tx_done(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(!app || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventReplayDone);
}

void flipper_app_gui_tx_done(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(!app || !app->capture) return;
    flipper_capture_tx_set_done_cb(app->capture, flipper_app_tx_done, app);
    flipper_app_gui_radio_release(app);
}
