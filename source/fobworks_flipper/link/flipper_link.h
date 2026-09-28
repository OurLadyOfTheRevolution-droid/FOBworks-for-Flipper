#pragma once
#include <furi.h>
#include "flipper_link_proto.h"

/*
 * Transport wrapper for the shared wire protocol in flipper_link_proto.*.
 * USB CDC serves a tethered Web Serial dashboard. GPIO USART on pins 13 (TX)
 * and 14 (RX), at 115200 8N1, connects to the ESP32 WiFi Devboard bridge.
 *
 * Both links can be active at once. Each gets its own RX thread and TX lock.
 * Parsed inbound lines are passed to flipper_app_handle_command() in
 * flipper_link_dispatch.c.
 */

typedef enum {
    FlipperLinkUsb,
    FlipperLinkUart,
} FlipperLinkTransport;

typedef struct FlipperLink FlipperLink;

/* Allocate the link and start its RX thread; pass app_ctx to the dispatcher. */
FlipperLink* flipper_link_alloc(void* app_ctx, FlipperLinkTransport transport);

/* Stop the RX thread and release the transport. */
void flipper_link_free(FlipperLink* link);

/* Thread-safe write of a formatted, newline-terminated line. */
void flipper_link_send_line(FlipperLink* link, const char* data, size_t len);

/* Return this link's transport type. */
FlipperLinkTransport flipper_link_transport(const FlipperLink* link);
uint32_t flipper_link_session_id(const FlipperLink* link);

/* ── Implemented by the app (flipper_link_dispatch.c) ─────────────────────── */
/* Called on the RX thread for each parsed command. Send replies to `origin`,
   the link that received the command, rather than broadcasting them. */
void flipper_app_handle_command(void* app_ctx, const FlipperCmd* cmd, FlipperLink* origin);
void flipper_app_link_disconnected(void* app_ctx, FlipperLink* link);
