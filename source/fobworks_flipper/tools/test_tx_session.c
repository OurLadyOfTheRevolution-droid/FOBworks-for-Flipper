#include "../protocol/flipper_tx_session.h"
#include <stdio.h>

static int checks;
static int failures;

static void expect(int ok, const char* label) {
    checks++;
    if(!ok) {
        printf("  FAIL: %s\n", label);
        failures++;
    }
}

static void test_lifecycle(void) {
    FlipperTxSession s;
    flipper_tx_session_init(&s);
    expect(flipper_tx_session_begin(
               &s, 100, FlipperTxKindReplay, 7, 42, 1000, 1, 400, 1000) ==
               FlipperTxBeginOk, "begin");
    expect(s.operation_id != 42 && s.request_id == 42 &&
               s.deadline_ms == 1100, "generation, request and deadline");
    expect(flipper_tx_session_begin(
               &s, 101, FlipperTxKindJam, 8, 43, 1000, 1, 400, 1000) ==
               FlipperTxBeginBusy, "ownership conflict");
    expect(!flipper_tx_session_tick(&s, 1099), "before deadline");
    expect(flipper_tx_session_tick(&s, 1100), "deadline transition");
    expect(s.state == FlipperTxStateTimedOut, "timeout state");
    expect(flipper_tx_session_cancel_ex(
               &s, 0, 42, 7, FlipperTxCancelTimeout, 1101),
           "repeat cancel is idempotent");

    expect(flipper_tx_session_begin(
               &s, 200, FlipperTxKindSequence, 7, 99, 2000, 2, 800, 2000) ==
               FlipperTxBeginOk, "second begin");
    expect(!flipper_tx_session_cancel(&s, 98, FlipperTxCancelBack, 201),
           "stale id rejected");
    expect(flipper_tx_session_cancel(&s, 99, FlipperTxCancelBack, 201),
           "matching cancel");
    expect(s.state == FlipperTxStateCancelled, "cancel state");
    expect(flipper_tx_session_cancel(&s, 99, FlipperTxCancelBack, 202),
           "cancel remains idempotent");
    expect(!flipper_tx_deadline_reached(0xFFFFFFF0u, 0x00000010u),
           "wrap before deadline");
    expect(flipper_tx_deadline_reached(0x00000010u, 0xFFFFFFF0u),
           "wrap reached");
    expect(flipper_tx_deadline_remaining(0x00000020u, 0xFFFFFFF0u) == 0,
           "remaining clamp");
    s.operation_id = UINT32_MAX;
    expect(flipper_tx_session_begin(
               &s, 300, FlipperTxKindReplay, 55, 7, 100, 1, 1, 2) ==
               FlipperTxBeginOk && s.operation_id == 1,
           "generation wrap skips zero");
    expect(!flipper_tx_session_cancel_ex(
               &s, 1, 7, 56, FlipperTxCancelBack, 301),
           "owner scoped stale stop");
    expect(flipper_tx_session_cancel_ex(
               &s, 1, 7, 55, FlipperTxCancelBack, 301),
           "owner scoped matching stop");
}

static void test_policy(void) {
    FlipperTxSession s;
    flipper_tx_session_init(&s);
    expect(flipper_tx_session_begin(
               &s, 0, FlipperTxKindJam, 0, 1, 100, 1, 900, 1000) ==
               FlipperTxBeginPolicy, "duty policy");
    expect(flipper_tx_session_begin(
               &s, 0, FlipperTxKindJam, 0, 1, 100, 9, 100, 1000) ==
               FlipperTxBeginPolicy, "frame policy");
    expect(flipper_tx_session_begin(
               &s, 0, FlipperTxKindJam, 0, 1, 600, 1, 400000, 500000) ==
               FlipperTxBeginOk, "bounded 80 percent jam accepted");
}

int main(void) {
    test_lifecycle();
    test_policy();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}