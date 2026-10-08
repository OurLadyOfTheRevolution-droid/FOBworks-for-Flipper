#!/usr/bin/env python3
"""I measure selected production ARM stack frames with the SDK compiler.

I run ufbt first to generate .vscode/compile_commands.json. This recompiles only
the selected translation units in a temporary directory; it never replaces
the SDK build's objects. Frame checks are NOT whole-call-chain or hardware
stack qualification."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "source/fobworks_flipper"
# Leave room for callers, SDK functions, interrupts and thread bookkeeping.
BUDGETS = {
    "flipper_tick_cb": ("flipper_fobscan_app.c", 512),
    "flipper_app_handle_command": ("flipper_link_dispatch.c", 1536),
    "feed_byte": ("flipper_link.c", 512),
    "usb_rx_thread": ("flipper_link.c", 128),
    "uart_rx_thread": ("flipper_link.c", 128),
    "flipper_capture_notify_worker": ("flipper_capture.c", 256),
    "flipper_capture_start": ("flipper_capture.c", 256),
    "flipper_scene_fobscan_on_enter": ("flipper_scene_fobscan.c", 256),
    "flipper_fobscan_draw_cb": ("flipper_scene_fobscan.c", 64),
    "fobscan_redraw": ("flipper_scene_fobscan.c", 192),
    "flipper_fobscan_display_build": ("flipper_fobscan_display.c", 256),
    "fobscan_consume_flush": ("flipper_scene_fobscan.c", 512),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compile-db", type=Path,
        default=APP / ".vscode/compile_commands.json")
    parser.add_argument("--report", type=Path, default=ROOT / "STACK_USAGE.md")
    args = parser.parse_args()
    entries = json.loads(args.compile_db.read_text())
    wanted = {file for file, _ in BUDGETS.values()}
    frames = {}
    compiled = set()
    with tempfile.TemporaryDirectory(prefix="fobworks-stack-") as tmp:
        for entry in entries:
            source = Path(entry["file"]).name
            if source not in wanted or source in compiled:
                continue
            command = entry.get("arguments") or shlex.split(entry["command"])
            command = list(command)
            output = command.index("-o") + 1
            obj = Path(tmp) / (source + ".o")
            command[output] = str(obj)
            command.extend(["-fstack-usage"])
            subprocess.run(command, cwd=entry.get("directory", APP), check=True)
            compiled.add(source)
        if compiled != wanted:
            raise RuntimeError(f"Missing compile commands: {wanted - compiled}")
        for usage in Path(tmp).glob("*.su"):
            for line in usage.read_text().splitlines():
                location, count, kind = line.split("\t")
                name = location.rsplit(":", 1)[-1]
                if name in BUDGETS:
                    if kind != "static":
                        raise RuntimeError(f"{name}: unqualified stack usage {kind}")
                    if name in frames:
                        raise RuntimeError(f"Duplicate frame measurement: {name}")
                    frames[name] = int(count)
    if set(frames) != set(BUDGETS):
        raise RuntimeError(f"Missing stack measurements: {set(BUDGETS) - set(frames)}")
    for name, count in frames.items():
        limit = BUDGETS[name][1]
        if count > limit:
            raise RuntimeError(f"{name}: {count} bytes exceeds frame budget {limit}")
    fap = APP / "dist/fobworks_flipper.fap"
    digest = hashlib.sha256(fap.read_bytes()).hexdigest()
    rows = [
        "# ARM stack-frame checks", "",
        "Compiler: the official 1.4.3 SDK compiler and production compile commands.",
        "FAP SHA-256: `" + digest + "`", "",
        "| Function | Measured frame (bytes) | Frame budget (bytes) |",
        "| --- | ---: | ---: |",
    ]
    rows.extend(
        f"| `{name}` | {frames[name]} | {BUDGETS[name][1]} |"
        for name in BUDGETS)
    rows.extend([
        "",
        "I measured individual compiler-reported frames, not total thread usage.",
        "Link workers and the app have 4096-byte stacks; the capture-notify worker",
        "and official GUI service have separate 2048-byte stacks.",
        "flipper_tick_cb runs on the app-owned event loop, not the firmware's",
        "1024-byte TimersSrv. Its scene/decoder/format callees still add frames.",
        "I still need to check SDK callees, plugin loading, interrupt/context saves",
        "and runtime high-water marks on the device.",
        "",
        "I did not capture the MPU fault's thread or PC. I verified the corrected",
        "command-handler overflow, but I have not established the exact cause",
        "of my FOBscan crash.",
        "",
    ])
    args.report.write_text("\n".join(rows))
    print("\n".join(f"{name}: {frames[name]} / {BUDGETS[name][1]}" for name in BUDGETS))
    print(f"Stack-frame checks passed; report: {args.report}")


if __name__ == "__main__":
    main()
