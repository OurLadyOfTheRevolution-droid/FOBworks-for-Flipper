#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * TX session and safety rules, kept independent of Furi and radio code so host
 * tests can exercise ownership, deadlines, and cancellation. The CC1101 adapter
 * uses the same contract on device.
 */
typedef enum {
    FlipperTxKindNone = 0,
    FlipperTxKindReplay,
    FlipperTxKindJam,
    FlipperTxKindSequence,
    FlipperTxKindDirect,
} FlipperTxKind;

typedef enum {
    FlipperTxStateIdle = 0,
    FlipperTxStateRunning,
    FlipperTxStateComplete,
    FlipperTxStateCancelled,
    FlipperTxStateTimedOut,
    FlipperTxStateBusy,
    FlipperTxStateRejected,
    FlipperTxStateError,
} FlipperTxState;

typedef enum {
    FlipperTxCancelNone = 0,
    FlipperTxCancelBack,
    FlipperTxCancelSceneExit,
    FlipperTxCancelDisconnect,
    FlipperTxCancelTimeout,
    FlipperTxCancelTick,
    FlipperTxCancelAppExit,
    FlipperTxCancelError,
} FlipperTxCancelReason;

typedef enum {
    FlipperTxBeginOk = 0,
    FlipperTxBeginBusy,
    FlipperTxBeginInvalid,
    FlipperTxBeginPolicy,
} FlipperTxBeginResult;

typedef struct {
    uint32_t max_duration_ms;
    uint32_t max_frames;
    uint8_t max_duty_cycle_pct;
} FlipperTxPolicy;

typedef struct {
    FlipperTxState state;
    FlipperTxKind kind;
    FlipperTxCancelReason cancel_reason;
    FlipperTxPolicy policy;
    uint32_t operation_id;
    uint32_t request_id; /* correlates external requests; ownership uses owner_id */
    uint32_t owner_id;
    uint32_t started_ms;
    uint32_t deadline_ms;
    uint32_t frame_limit;
    uint32_t frames_sent;
    uint32_t duty_on_us;
    uint32_t duty_total_us;
} FlipperTxSession;

void flipper_tx_session_init(FlipperTxSession* s);
FlipperTxBeginResult flipper_tx_session_begin(
    FlipperTxSession* s,
    uint32_t now_ms,
    FlipperTxKind kind,
    uint32_t owner_id,
    uint32_t requested_id,
    uint32_t duration_ms,
    uint32_t frame_count,
    uint32_t duty_on_us,
    uint32_t duty_total_us);
bool flipper_tx_session_tick(FlipperTxSession* s, uint32_t now_ms);
bool flipper_tx_session_frame(FlipperTxSession* s);
bool flipper_tx_session_finish(FlipperTxSession* s, uint32_t now_ms);
bool flipper_tx_session_cancel(
    FlipperTxSession* s,
    uint32_t matching_id,
    FlipperTxCancelReason reason,
    uint32_t now_ms);
bool flipper_tx_session_cancel_ex(
    FlipperTxSession* s,
    uint32_t matching_generation,
    uint32_t matching_request_id,
    uint32_t matching_owner_id,
    FlipperTxCancelReason reason,
    uint32_t now_ms);
bool flipper_tx_session_stop(
    FlipperTxSession* s,
    uint32_t matching_id,
    FlipperTxCancelReason reason,
    uint32_t now_ms);
bool flipper_tx_session_is_active(const FlipperTxSession* s);
bool flipper_tx_deadline_reached(uint32_t now_ms, uint32_t deadline_ms);
uint32_t flipper_tx_deadline_remaining(uint32_t now_ms, uint32_t deadline_ms);
const char* flipper_tx_state_name(FlipperTxState state);
const char* flipper_tx_cancel_name(FlipperTxCancelReason reason);
