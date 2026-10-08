#!/usr/bin/env python3
"""Regression guard for periodic callback ownership and standalone work.

I check the production lifecycle wiring, not physical-device responsiveness.
I run with check_stack_usage.py after building with the official SDK."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "source/fobworks_flipper/flipper_fobscan_app.c").read_text()
header = (root / "source/fobworks_flipper/flipper_fobscan_app.h").read_text()


def body(name):
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", source)
    assert match, f"missing production function {name}"
    start = match.end()
    depth = 1
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end]
    raise AssertionError(f"unclosed function {name}")


assert "FuriEventLoopTimer* tick_timer;" in header
assert not re.search(r"\bfuri_timer_(alloc|start|stop|free)\s*\(", source)
alloc = body("flipper_app_alloc")
assert re.search(
    r"furi_event_loop_timer_alloc\(\s*"
    r"view_dispatcher_get_event_loop\(app->view_dispatcher\),\s*"
    r"flipper_tick_cb,\s*FuriEventLoopTimerTypePeriodic,\s*app\)", alloc)
tick = body("flipper_tick_cb")
assert "view_dispatcher_send_custom_event(" not in tick
dispatch = tick.index(
    "scene_manager_handle_custom_event(app->scene_manager, FlipperEventStatusTick)")
guard = tick.index("if(!app->usb_link && !app->uart_link) return;")
remote = tick.index("if(app->remote_scanning")
heartbeat = tick.index("flipper_proto_emit_heartbeat(")
assert dispatch < guard < remote < heartbeat
free = body("flipper_app_free")
assert free.index("furi_event_loop_timer_stop(") < free.index("furi_event_loop_timer_free(")
assert free.index("furi_event_loop_timer_free(") < free.index("view_dispatcher_free(")
assert "furi_event_loop_timer_start(app->tick_timer, furi_ms_to_ticks(500))" in body(
    "flipper_fobscan_app")
print("PASS: periodic callback owned by app loop; direct scene dispatch; "
      "standalone dashboard-work guard; ordered lifecycle")
