#include "flipper_tx_session.h"
#include <string.h>

#define TX_DEFAULT_MAX_DURATION_MS 30000u
#define TX_DEFAULT_MAX_FRAMES 8u
#define TX_DEFAULT_MAX_DUTY_PCT 80u

/* Compare deadlines with signed subtraction so the check remains valid when
   the 32-bit tick counter wraps. */
bool flipper_tx_deadline_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

uint32_t flipper_tx_deadline_remaining(uint32_t now, uint32_t deadline) {
    if(flipper_tx_deadline_reached(now, deadline)) return 0;
    return deadline - now;
}

void flipper_tx_session_init(FlipperTxSession* s) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->state = FlipperTxStateIdle;
    s->policy.max_duration_ms = TX_DEFAULT_MAX_DURATION_MS;
    s->policy.max_frames = TX_DEFAULT_MAX_FRAMES;
    s->policy.max_duty_cycle_pct = TX_DEFAULT_MAX_DUTY_PCT;
}

bool flipper_tx_session_is_active(const FlipperTxSession* s) {
    return s && s->state == FlipperTxStateRunning;
}

FlipperTxBeginResult flipper_tx_session_begin(
    FlipperTxSession* s,
    uint32_t now_ms,
    FlipperTxKind kind,
    uint32_t owner_id,
    uint32_t requested_id,
    uint32_t duration_ms,
    uint32_t frame_count,
    uint32_t duty_on_us,
    uint32_t duty_total_us) {
    if(!s || kind == FlipperTxKindNone || duration_ms == 0 || frame_count == 0)
        return FlipperTxBeginInvalid;
    if(flipper_tx_session_is_active(s)) {
        return FlipperTxBeginBusy;
    }
    if(duration_ms > s->policy.max_duration_ms ||
       frame_count > s->policy.max_frames || duty_total_us == 0 ||
       duty_on_us > duty_total_us ||
       (uint64_t)duty_on_us * 100u >
           (uint64_t)duty_total_us * s->policy.max_duty_cycle_pct) {
        s->state = FlipperTxStateRejected;
        return FlipperTxBeginPolicy;
    }

    uint32_t generation = s->operation_id + 1u;
    if(generation == 0) generation = 1;
    memset(s, 0, sizeof(*s));
    s->state = FlipperTxStateRunning;
    s->kind = kind;
    s->policy.max_duration_ms = TX_DEFAULT_MAX_DURATION_MS;
    s->policy.max_frames = TX_DEFAULT_MAX_FRAMES;
    s->policy.max_duty_cycle_pct = TX_DEFAULT_MAX_DUTY_PCT;
    s->operation_id = generation;
    s->request_id = requested_id;
    s->owner_id = owner_id;
    s->started_ms = now_ms;
    s->deadline_ms = now_ms + duration_ms;
    s->frame_limit = frame_count;
    s->duty_on_us = duty_on_us;
    s->duty_total_us = duty_total_us;
    return FlipperTxBeginOk;
}

bool flipper_tx_session_tick(FlipperTxSession* s, uint32_t now_ms) {
    if(!flipper_tx_session_is_active(s)) return false;
    if(flipper_tx_deadline_reached(now_ms, s->deadline_ms)) {
        s->state = FlipperTxStateTimedOut;
        s->cancel_reason = FlipperTxCancelTimeout;
        return true;
    }
    return false;
}

bool flipper_tx_session_frame(FlipperTxSession* s) {
    if(!flipper_tx_session_is_active(s) ||
       s->frames_sent >= s->frame_limit)
        return false;
    s->frames_sent++;
    return true;
}

bool flipper_tx_session_finish(FlipperTxSession* s, uint32_t now_ms) {
    if(!flipper_tx_session_is_active(s)) return false;
    if(flipper_tx_session_tick(s, now_ms)) return false;
    if(s->frames_sent == 0 || s->frames_sent > s->frame_limit) {
        s->state = FlipperTxStateError;
        s->cancel_reason = FlipperTxCancelError;
        return false;
    }
    s->state = FlipperTxStateComplete;
    return true;
}

bool flipper_tx_session_cancel(
    FlipperTxSession* s,
    uint32_t matching_id,
    FlipperTxCancelReason reason,
    uint32_t now_ms) {
    if(!s) return false;
    return flipper_tx_session_cancel_ex(
        s, 0, matching_id, 0, reason, now_ms);
}

bool flipper_tx_session_cancel_ex(
    FlipperTxSession* s,
    uint32_t matching_generation,
    uint32_t matching_request_id,
    uint32_t matching_owner_id,
    FlipperTxCancelReason reason,
    uint32_t now_ms) {
    if(!s) return false;
    if(!flipper_tx_session_is_active(s)) {
        /* Repeating a cancellation is harmless. A stale ID cannot change the
           current operation; an unqualified repeat has nothing left to cancel. */
        return (matching_generation == 0 || matching_generation == s->operation_id) &&
               (matching_request_id == 0 || matching_request_id == s->request_id) &&
               (matching_owner_id == 0 || matching_owner_id == s->owner_id);
    }
    if(matching_generation != 0 && matching_generation != s->operation_id) return false;
    if(matching_request_id != 0 && matching_request_id != s->request_id) return false;
    if(matching_owner_id != 0 && matching_owner_id != s->owner_id) return false;
    if(flipper_tx_session_tick(s, now_ms)) return true;
    s->state = FlipperTxStateCancelled;
    s->cancel_reason = reason ? reason : FlipperTxCancelError;
    return true;
}

bool flipper_tx_session_stop(
    FlipperTxSession* s,
    uint32_t matching_id,
    FlipperTxCancelReason reason,
    uint32_t now_ms) {
    return flipper_tx_session_cancel(s, matching_id, reason, now_ms);
}

const char* flipper_tx_state_name(FlipperTxState state) {
    switch(state) {
    case FlipperTxStateRunning: return "running";
    case FlipperTxStateComplete: return "complete";
    case FlipperTxStateCancelled: return "cancelled";
    case FlipperTxStateTimedOut: return "timeout";
    case FlipperTxStateBusy: return "busy";
    case FlipperTxStateRejected: return "policy";
    case FlipperTxStateError: return "error";
    case FlipperTxStateIdle:
    default: return "idle";
    }
}

const char* flipper_tx_cancel_name(FlipperTxCancelReason reason) {
    switch(reason) {
    case FlipperTxCancelBack: return "back";
    case FlipperTxCancelSceneExit: return "scene-exit";
    case FlipperTxCancelDisconnect: return "disconnect";
    case FlipperTxCancelTimeout: return "timeout";
    case FlipperTxCancelTick: return "tick";
    case FlipperTxCancelAppExit: return "app-exit";
    case FlipperTxCancelError: return "error";
    case FlipperTxCancelNone:
    default: return "none";
    }
}