#pragma once
#include <furi.h>
#include "flipper_link_proto.h"

/*
 * Transport wrapper around the pure wire protocol (flipper_link_proto.*).
 *
 * Two backends share one implementation:
 *   FlipperLinkUsb  — USB CDC virtual COM port (tethered Web Serial dashboard).
 *   FlipperLinkUart — GPIO USART on pins 13(TX)/14(RX) at 115200 8N1, used by
 *                     the ESP32 WiFi Devboard bridge (sgp_flipper_wifi_bridge).
 *
 * Both may run at once: the app allocates one link per transport, each with its
 * own RX thread and TX lock, so a USB host and the WiFi bridge can be attached
 * simultaneously without cross-talk.
 *
 * Inbound lines are parsed and handed to flipper_app_handle_command(), which
 * the app implements (flipper_link_dispatch.c).
 */

typedef enum {
    FlipperLinkUsb,
    FlipperLinkUart,
} FlipperLinkTransport;

typedef struct FlipperLink FlipperLink;

/* Allocate + start the RX thread. app_ctx is passed back to the dispatcher. */
FlipperLink* flipper_link_alloc(void* app_ctx, FlipperLinkTransport transport);

/* Stop the RX thread and release the transport. */
void flipper_link_free(FlipperLink* link);

/* Thread-safe write of a pre-formatted (newline-terminated) line. */
void flipper_link_send_line(FlipperLink* link, const char* data, size_t len);

/* Which transport this link drives (for logging / diagnostics). */
FlipperLinkTransport flipper_link_transport(const FlipperLink* link);
uint32_t flipper_link_session_id(const FlipperLink* link);

/* ── Implemented by the app (flipper_link_dispatch.c) ─────────────────────── */
/* Called from the link RX thread for every parsed inbound command. `origin`
   is the link the command arrived on — send replies back through it. */
void flipper_app_handle_command(void* app_ctx, const FlipperCmd* cmd, FlipperLink* origin);
void flipper_app_link_disconnected(void* app_ctx, FlipperLink* link);
