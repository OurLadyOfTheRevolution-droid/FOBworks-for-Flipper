#include "flipper_link.h"
#include <furi_hal.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <string.h>

/*
 * The USB CDC channel and async-RX API vary between firmware releases
 * (Official, Unleashed, and RogueMaster). Keep this hardware-specific layer
 * separate from flipper_link_proto.* so HAL changes do not alter the wire
 * schema. The protocol layer has host tests; this transport glue is checked
 * on-device.
 */

#define LINK_TX_BUF   512
#define LINK_RX_LINE  256
#define LINK_USB_VCP  0          /* CDC channel used for the dashboard link    */
/* Parsing and dispatch run on the RX thread. Their local buffers and call
   frames exceed 1 KB, which triggered the MPU guard; 4 KB leaves headroom. */
#define LINK_RX_STACK 4096

struct FlipperLink {
    FlipperLinkTransport transport;
    void*                app_ctx;
    bool                 running;
    uint32_t             session_id;
    volatile bool        disconnect_requested;
    volatile bool        usb_ready;
    FuriMutex*           tx_mutex;

    /* USB backend */
    FuriThread*          rx_thread;
    void*                usb_prev_config;  /* CLI's USB config, restored on free */

    /* UART backend */
    FuriHalSerialHandle* serial;
    FuriStreamBuffer*    rx_stream;   /* bytes pushed from the serial ISR      */

    /* line assembly */
    char                 line[LINK_RX_LINE];
    int                  lp;
};

static uint32_t link_session_next = 1;

/* ── Line assembly shared by both transports ──────────────────────────────── */
static void feed_byte(FlipperLink* link, char c) {
    if(c == '\n' || c == '\r') {
        if(link->lp > 0) {
            link->line[link->lp] = '\0';
            FlipperCmd cmd;
            if(flipper_proto_parse_cmd(link->line, &cmd))
                flipper_app_handle_command(link->app_ctx, &cmd, link);
            link->lp = 0;
        }
    } else if(link->lp < (int)sizeof(link->line) - 1) {
        link->line[link->lp++] = c;
    } else {
        link->lp = 0; /* overflow — drop the malformed line */
    }
}

/* ── USB CDC transport ────────────────────────────────────────────────────── */
#define LINK_USB_RX_EVT (1UL << 0)
#define LINK_USB_STATE_EVT (1UL << 1)

/* Runs in USB-stack context: only signal the worker; never touch the radio or
   the decode engine from here. */
static void usb_cdc_rx_ep_cb(void* ctx) {
    FlipperLink* link = (FlipperLink*)ctx;
    if(link && link->rx_thread)
        furi_thread_flags_set(furi_thread_get_id(link->rx_thread), LINK_USB_RX_EVT);
}

static void usb_cdc_state_cb(void* ctx, CdcState state) {
    FlipperLink* link = (FlipperLink*)ctx;
    if(!link) return;
    link->usb_ready = state == CdcStateConnected;
    if(!link->usb_ready) link->disconnect_requested = true;
    if(link->rx_thread)
        furi_thread_flags_set(
            furi_thread_get_id(link->rx_thread), LINK_USB_STATE_EVT);
}

static CdcCallbacks link_cdc_cb = {
    .tx_ep_callback     = NULL,
    .rx_ep_callback     = usb_cdc_rx_ep_cb,
    .state_callback     = usb_cdc_state_cb,
    .ctrl_line_callback = NULL,
    .config_callback    = NULL,
};

static int32_t usb_rx_thread(void* ctx) {
    FlipperLink* link = (FlipperLink*)ctx;
    uint8_t buf[64];
    while(link->running) {
        /* Wake on an RX-endpoint event, or time out to re-check ->running. */
        furi_thread_flags_wait(LINK_USB_RX_EVT, FuriFlagWaitAny, 50);
        if(link->disconnect_requested) {
            link->disconnect_requested = false;
            flipper_app_link_disconnected(link->app_ctx, link);
        }
        size_t got;
        do {
            got = furi_hal_cdc_receive(LINK_USB_VCP, buf, sizeof(buf));
            for(size_t i = 0; i < got; i++) feed_byte(link, (char)buf[i]);
        } while(got > 0);
    }
    return 0;
}

/* ── UART transport (GPIO pins 13/14) ─────────────────────────────────────── */
static void uart_rx_isr(FuriHalSerialHandle* handle, FuriHalSerialRxEvent ev, void* ctx) {
    FlipperLink* link = (FlipperLink*)ctx;
    if(ev == FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(link->rx_stream, &b, 1, 0);
    } else link->disconnect_requested = true;
}

static int32_t uart_rx_thread(void* ctx) {
    FlipperLink* link = (FlipperLink*)ctx;
    uint8_t byte;
    while(link->running) {
        size_t got = furi_stream_buffer_receive(link->rx_stream, &byte, 1,
                                                furi_ms_to_ticks(50));
        if(got > 0) feed_byte(link, (char)byte);
        if(link->disconnect_requested) {
            link->disconnect_requested = false;
            flipper_app_link_disconnected(link->app_ctx, link);
        }
    }
    return 0;
}

/* ── Alloc / free ─────────────────────────────────────────────────────────── */
FlipperLink* flipper_link_alloc(void* app_ctx, FlipperLinkTransport transport) {
    FlipperLink* link = malloc(sizeof(FlipperLink));
    if(!link) return NULL;
    memset(link, 0, sizeof(*link));
    link->transport = transport;
    link->app_ctx   = app_ctx;
    link->running   = true;
    link->session_id = link_session_next++;
    if(link->session_id == 0) link->session_id = link_session_next++;
    link->tx_mutex  = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!link->tx_mutex) {
        free(link);
        return NULL;
    }

    if(transport == FlipperLinkUart) {
        link->rx_stream = furi_stream_buffer_alloc(LINK_RX_LINE, 1);
        if(!link->rx_stream) {
            furi_mutex_free(link->tx_mutex);
            free(link);
            return NULL;
        }
        link->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
        if(link->serial) {
            furi_hal_serial_init(link->serial, 115200);
            furi_hal_serial_async_rx_start(link->serial, uart_rx_isr, link, false);
        }
        link->rx_thread = furi_thread_alloc_ex("fob_uart_rx", LINK_RX_STACK, uart_rx_thread, link);
        if(!link->rx_thread) {
            if(link->serial) {
                furi_hal_serial_async_rx_stop(link->serial);
                furi_hal_serial_deinit(link->serial);
                furi_hal_serial_control_release(link->serial);
            }
            furi_stream_buffer_free(link->rx_stream);
            furi_mutex_free(link->tx_mutex);
            free(link);
            return NULL;
        }
        furi_thread_start(link->rx_thread);
    } else {
        /* Prepare the worker but do not start it until the USB interface
           transition has succeeded.  That keeps allocation transactional:
           a failed transition cannot leave a live worker pointing at a link
           that is about to be freed. */
        link->rx_thread = furi_thread_alloc_ex("fob_usb_rx", LINK_RX_STACK, usb_rx_thread, link);
        if(!link->rx_thread) {
            furi_mutex_free(link->tx_mutex);
            free(link);
            return NULL;
        }
        /* Take the USB VCP away from the CLI.  Save the previous interface
           before unlocking because a failed transition must restore it. */
        link->usb_prev_config = furi_hal_usb_get_config();
        furi_hal_usb_unlock();
        if(!furi_hal_usb_set_config(&usb_cdc_single, NULL)) {
            if(link->usb_prev_config) {
                furi_hal_usb_unlock();
                furi_hal_usb_set_config(
                    (FuriHalUsbInterface*)link->usb_prev_config, NULL);
            }
            /* The thread was never started, so it is safe to free directly. */
            furi_thread_free(link->rx_thread);
            furi_mutex_free(link->tx_mutex);
            free(link);
            return NULL;
        }
        /* No callback can have observed this link before this point. */
        furi_thread_start(link->rx_thread);
        furi_hal_cdc_set_callbacks(LINK_USB_VCP, &link_cdc_cb, link);
    }
    return link;
}

void flipper_link_free(FlipperLink* link) {
    if(!link) return;
    /* Detach callback context before stopping/freeing the worker it signals. */
    if(link->transport == FlipperLinkUsb)
        furi_hal_cdc_set_callbacks(LINK_USB_VCP, NULL, NULL);
    link->running = false;
    /* Wake the USB worker so it observes ->running == false promptly. */
    if(link->transport == FlipperLinkUsb && link->rx_thread)
        furi_thread_flags_set(furi_thread_get_id(link->rx_thread), LINK_USB_RX_EVT);
    if(link->rx_thread) {
        furi_thread_join(link->rx_thread);
        furi_thread_free(link->rx_thread);
    }

    /* The RX worker is fully quiescent now, so cancellation cannot race a
       command that still owns the radio. */
    flipper_app_link_disconnected(link->app_ctx, link);

    if(link->transport == FlipperLinkUart) {
        if(link->serial) {
            furi_hal_serial_async_rx_stop(link->serial);
            furi_hal_serial_deinit(link->serial);
            furi_hal_serial_control_release(link->serial);
        }
        if(link->rx_stream) furi_stream_buffer_free(link->rx_stream);
    } else {
        /* Hand the USB VCP back to the CLI. */
        if(link->usb_prev_config) {
            furi_hal_usb_unlock();
            furi_hal_usb_set_config((FuriHalUsbInterface*)link->usb_prev_config, NULL);
        }
    }
    if(link->tx_mutex) furi_mutex_free(link->tx_mutex);
    free(link);
}

/* ── TX ───────────────────────────────────────────────────────────────────── */
void flipper_link_send_line(FlipperLink* link, const char* data, size_t len) {
    if(!link || !data || !len) return;
    furi_mutex_acquire(link->tx_mutex, FuriWaitForever);
    if(link->transport == FlipperLinkUart) {
        if(link->serial)
            furi_hal_serial_tx(link->serial, (const uint8_t*)data, len);
    } else {
        /* Non-blocking: silently drops if no host is attached. */
        furi_hal_cdc_send(LINK_USB_VCP, (uint8_t*)data, (uint16_t)len);
    }
    furi_mutex_release(link->tx_mutex);
}

FlipperLinkTransport flipper_link_transport(const FlipperLink* link) {
    return link->transport;
}

uint32_t flipper_link_session_id(const FlipperLink* link) {
    return link ? link->session_id : 0;
}
