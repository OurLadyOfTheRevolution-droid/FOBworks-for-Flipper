#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <stdint.h>
#include <stdbool.h>

#include "flipper_decoders.h"
#include "flipper_tx_session.h"

/* ── CC1101 preset selection ─────────────────────────────────────────────── */
typedef enum FlipperPreset {
    FlipperPresetOOK650,       /* OOK, 650 kHz bandwidth — general purpose        */
    FlipperPresetOOK270,       /* OOK, 270 kHz bandwidth — narrow band            */
    FlipperPreset2FSKDev238,   /* 2FSK, ±23.8 kHz deviation                       */
    FlipperPreset2FSKDev476,   /* 2FSK, ±47.6 kHz deviation                       */
} FlipperPreset;

/* ── Capture result ───────────────────────────────────────────────────────── */
typedef struct FlipperCaptureResult {
    FlipperPulseBuf    pulses;
    FlipperDecodeResult decode;
    bool           decode_ok;
    uint32_t       timestamp_ms;
} FlipperCaptureResult;

typedef struct {
    const uint32_t* durations;
    int count;
    int pos;
    bool level;
} FlipperTxWaveState;

typedef enum {
    FlipperTxWorkIdle = 0,
    FlipperTxWorkPending,
    FlipperTxWorkActive,
    FlipperTxWorkDelay,
} FlipperTxWorkState;

/* ── Capture engine state ────────────────────────────────────────────────── */
#define FLIPPER_CAP_EDGE_MAX FLIPPER_PULSE_MAX
#define FLIPPER_TX_OWNER_GUI 0x80000001u

typedef struct {
    /* SubGHz device handle — acquired in alloc, released in free */
    const SubGhzDevice* device;

    /* Config — set before flipper_capture_start() */
    float     freq_mhz;
    FlipperPreset preset;
    uint32_t  timeout_ms;      /* 0 = no timeout (run until flipper_capture_stop) */
    float     squelch_dbm;     /* ignore bursts below this RSSI, e.g. -85.0f  */
    FlipperForceProto force_proto; /* Advanced Settings force-protocol selection */

    /* Async RX ring */
    volatile uint32_t edges[FLIPPER_CAP_EDGE_MAX];
    volatile bool     levels[FLIPPER_CAP_EDGE_MAX];
    volatile int      edge_head;
    volatile int      edge_tail;
    FuriMutex*        edge_mutex;

    /* State */
    bool      running;
    bool      capture_done;
    uint32_t  start_ms;

    /*     * Burst-level debounce flag — set by flipper_capture_rx_cb the first time * it fires on_edge for a new burst; cleared by flipper_capture_flush() so * the next burst edge will fire again. Without this, I found a 100-edge fob burst * queues 100 events into the ViewDispatcher before the first one is * processed, flooding the event queue. */
    volatile bool edge_pending;
    volatile int edge_mark;
    volatile uint8_t edge_quiet;
    /* Flushes before this tick are the radio-open burst and are discarded. */
    uint32_t rx_hold_until;

    /* Latest result */
    FlipperCaptureResult result;

    /* Callback invoked when a new burst begins.  IMPORTANT: it is dispatched from a dedicated worker THREAD (flipper_capture_notify_worker), NOT from the async-RX ISR. The ISR only sets an event flag; the worker calls on_edge in thread context, so on_edge may safely use ViewDispatcher / FuriMessageQueue (FuriWaitForever) APIs. I found calling those from the ISR tripped furi_check() and crashed the instant scanning started. */
    void (*on_edge)(void* ctx);
    void* on_edge_ctx;

    /* ISR → thread hand-off for on_edge (see above). */
    FuriThread*    notify_thread;
    FuriEventFlag* notify_flag;
    volatile bool  notify_run;

    /* Shared TX lifecycle. All replay/jam/sequence paths use this state machine; the HAL adapter is not an independent TX entry point. */
    FlipperTxSession tx_session;
    bool             tx_running;
    FuriMutex*       tx_mutex;
    /* One bounded private waveform owned by the engine. TX never retains a caller/library/stack pulse pointer after enqueue returns. */
    FlipperPulseBuf tx_owned;
    FlipperTxWorkState tx_work;
    const FlipperPulseBuf* tx_buf;
    FlipperPulseBuf* tx_seq;
    int tx_seq_n;
    int tx_count;
    int tx_index;
    float tx_freq_mhz;
    FlipperPreset tx_preset;
    uint32_t tx_delay_ms;
    uint32_t tx_next_ms;
    bool tx_hal_active;
    FlipperTxWaveState tx_wave;
    void (*tx_done_cb)(void* ctx);
    void* tx_done_ctx;
    bool tx_callback_active;
    uint32_t         edge_overflow;
    uint32_t         peak_free_heap;
} FlipperCaptureEngine;

/* ── API ──────────────────────────────────────────────────────────────────── */

/* I allocate the engine and acquire the SubGHz device. I must call this from the main task. */
FlipperCaptureEngine* flipper_capture_alloc(void);

/* I stop capture, release the SubGHz device, and free the engine. */
void flipper_capture_free(FlipperCaptureEngine* e);

/* I start async RX. I reset and configure the CC1101, then begin edge capture. */
void flipper_capture_start(FlipperCaptureEngine* e);

/* I stop async RX and put the radio to sleep. Safe to call from any context. */
void flipper_capture_stop(FlipperCaptureEngine* e);

/* I flush the edge ring into a FlipperPulseBuf and attempt decode. I call this from the main task context (not ISR). Returns true when e->result is freshly populated. */
bool flipper_capture_flush(FlipperCaptureEngine* e);

/* I queue a pulse buffer for cooperative TX. I return once accepted/rejected; completion and HAL teardown occur on the lazy capture owner task. */
bool flipper_capture_tx(FlipperCaptureEngine* e, const FlipperPulseBuf* buf, float freq_mhz, FlipperPreset preset, uint32_t timeout_ms);

/* Explicit operation form I use for remote control and diagnostics. */
bool flipper_capture_tx_ex(
    FlipperCaptureEngine* e,
    const FlipperPulseBuf* buf,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t timeout_ms,
    FlipperTxKind kind,
    uint32_t requested_id);
bool flipper_capture_tx_ex_owner(
    FlipperCaptureEngine* e,
    const FlipperPulseBuf* buf,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t timeout_ms,
    FlipperTxKind kind,
    uint32_t requested_id,
    uint32_t owner_id,
    uint32_t frame_count,
    uint32_t delay_ms);
bool flipper_capture_tx_repeat(
    FlipperCaptureEngine* e,
    const FlipperPulseBuf* buf,
    uint32_t repeats,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t delay_ms);

/* Idempotent, matching-ID cancellation. ID 0 means the current operation. */
bool flipper_capture_tx_cancel(
    FlipperCaptureEngine* e,
    uint32_t operation_id,
    FlipperTxCancelReason reason);

bool flipper_capture_tx_cancel_ex(
    FlipperCaptureEngine* e,
    uint32_t generation,
    uint32_t request_id,
    uint32_t owner_id,
    FlipperTxCancelReason reason);
bool flipper_capture_tx_status_copy(
    const FlipperCaptureEngine* e, FlipperTxSession* out);
bool flipper_capture_tx_snapshot(
    const FlipperCaptureEngine* e, FlipperTxSession* out, bool* ready);
void flipper_capture_tx_set_done_cb(
    FlipperCaptureEngine* e, void (*cb)(void*), void* ctx);
void flipper_capture_tx_wait_stopped(FlipperCaptureEngine* e);

/* I transmit N consecutive captures in order (FOBback RollBack replay). */
bool flipper_capture_tx_sequence(
    FlipperCaptureEngine* e,
    const FlipperCaptureResult* caps,
    int count,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t delay_ms);

/* I sweep a list of frequencies, spending dwell_ms at each. Returns the frequency index that triggered, or -1 on timeout. */
int flipper_capture_sweep(
    FlipperCaptureEngine* e,
    const float* freqs_mhz,
    int count,
    uint32_t dwell_ms);

/* Current RSSI in dBm. Only valid while the radio is scanning. */
float flipper_capture_rssi(FlipperCaptureEngine* e);

/* Internal async RX callback — I do not call directly. */
void flipper_capture_rx_cb(bool level, uint32_t duration_us, void* ctx);
